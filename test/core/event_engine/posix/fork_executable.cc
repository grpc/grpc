#include <netinet/in.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <csignal>
#include <cstdio>
#include <iostream>
#include <memory>
#include <thread>

#include "absl/cleanup/cleanup.h"
#include "absl/flags/parse.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "absl/strings/cord.h"
#include "absl/strings/str_cat.h"
#include "absl/time/clock.h"

#include "include/grpc/event_engine/event_engine.h"
#include <grpc/fork.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/event_engine/channel_args_endpoint_config.h"
#include "src/core/lib/event_engine/posix_engine/event_poller_posix_default.h"
#include "src/core/lib/event_engine/posix_engine/posix_endpoint.h"
#include "src/core/lib/event_engine/posix_engine/posix_engine.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/gprpp/fork.h"
#include "test/core/event_engine/event_engine_test_utils.h"
#include "test/core/event_engine/posix/posix_engine_test_utils.h"
#include "test/core/test_util/port.h"

namespace {

using namespace ::grpc_event_engine::experimental;
using Endpoint = ::grpc_event_engine::experimental::EventEngine::Endpoint;
using Listener = ::grpc_event_engine::experimental::EventEngine::Listener;
using namespace std::chrono_literals;

class Server {
 public:
  Server()
      : port_(grpc_pick_unused_port_or_die()),
        server_thread_(ServerThread, this) {
    grpc_core::MutexLock lock(&mu_);
    while (!running_) {
      cond_.Wait(&mu_);
    }
  }

  ~Server() { server_thread_.join(); }

 private:
  static void ServerThread(Server* server) {
    {
      grpc_core::MutexLock lock(&server->mu_);
      server->running_ = true;
      server->cond_.SignalAll();
    }
  }

  int port_;
  grpc_core::Mutex mu_;
  grpc_core::CondVar cond_;
  bool running_ ABSL_GUARDED_BY(&mu_) = false;

  std::thread server_thread_;
};

class Reader {
 public:
  explicit Reader(int fd) : fd_(fd) {}
  ~Reader() { close(fd_); }

  std::string Read() {
    std::array<char, 100000> data;
    ssize_t r = read(fd_, data.data(), data.size());
    if (r > 0) {
      return std::string(data.data(), r);
    } else {
      return "";
    }
  }

 private:
  int fd_;
};

class Writer {
 public:
  explicit Writer(int fd) : fd_(fd) {}
  ~Writer() { close(fd_); }
  bool Write(absl::string_view data) {
    return write(fd_, data.data(), data.size()) >= 0;
  }

 private:
  int fd_;
};

// A helper class to drive the polling of Fds. It repeatedly calls the Work(..)
// method on the poller to get pet pending events, then schedules another
// parallel Work(..) instantiation and processes these pending events. This
// continues until all Fds have orphaned themselves.
class Worker : public grpc_core::DualRefCounted<Worker> {
 public:
  Worker(std::shared_ptr<EventEngine> engine, PosixEventPoller* poller)
      : engine_(std::move(engine)), poller_(poller) {
    WeakRef().release();
  }
  void Orphaned() override {
    grpc_core::MutexLock lock(&mu_);
    orphaned_ = true;
    cond_.Signal();
  }
  void Start() {
    // Start executing Work(..).
    engine_->Run([self = Ref()]() { self->Work(); });
  }

  void Wait() {
    grpc_core::MutexLock lock(&mu_);
    while (!orphaned_) {
      cond_.Wait(&mu_);
    }
    WeakUnref();
  }

 private:
  void Work() {
    auto result = poller_->Work(24h, [self = Ref()]() {
      // Schedule next work instantiation immediately and take a Ref for
      // the next instantiation.
      self->Ref().release();
      self->engine_->Run([self = self->Ref()]() { self->Work(); });
    });
    CHECK(result == Poller::WorkResult::kOk ||
          result == Poller::WorkResult::kKicked);
    // Corresponds to the Ref taken for the current instantiation. If the
    // result was Poller::WorkResult::kKicked, then the next work instantiation
    // would not have been scheduled and the poll_again callback would have
    // been deleted.
    Unref();
  }

  std::shared_ptr<EventEngine> engine_;
  // The poller is not owned by the Worker. Rather it is owned by the test
  // which creates the worker instance.
  PosixEventPoller* poller_;
  grpc_core::Mutex mu_;
  grpc_core::CondVar cond_;
  bool orphaned_ ABSL_GUARDED_BY(mu_) = false;
};

class EventEngineHolder {
 public:
  EventEngineHolder(const EventEngine::ResolvedAddress& address)
      : address_(address) {
    scheduler_ = std::make_unique<TestScheduler>();
    poller_ = MakeDefaultPoller(scheduler_.get());
    CHECK_NE(poller_, nullptr);
    event_engine_ = PosixEventEngine::MakeTestOnlyPosixEventEngine(poller_);
    scheduler_->ChangeCurrentEventEngine(event_engine_.get());
    Listener::AcceptCallback accept_cb =
        [this](std::unique_ptr<Endpoint> ep,
               grpc_core::MemoryAllocator /*memory_allocator*/) mutable {
          absl::MutexLock lock(&mu_);
          CHECK_EQ(server_endpoint_.get(), nullptr)
              << "Previous endpoint was not claimed";
          server_endpoint_ = std::move(ep);
          cond_.SignalAll();
        };
    ChannelArgsEndpointConfig config(BuildChannelArgs());
    auto l = event_engine_->CreateListener(
        std::move(accept_cb),
        [this](const absl::Status& status) {
          grpc_core::MutexLock lock(&mu_);
          listener_shutdown_status_ = status;
          cond_.SignalAll();
        },
        config, std::make_unique<grpc_core::MemoryQuota>("foo"));
    CHECK_OK(l);
    listener_ = std::move(l).value();
    absl::Status status = listener_->Bind(address_).status();
    CHECK_OK(status);
    status = listener_->Start();
    CHECK_OK(status);
    worker_ = new Worker(event_engine_, poller_.get());
    worker_->Start();
  }

  ~EventEngineHolder() {
    if (worker_ != nullptr) {
      worker_->Wait();
    }
    listener_.reset();
    WaitForSingleOwnerWithTimeout(std::move(event_engine_),
                                  std::chrono::seconds(30));
  }

  bool ok() const { return poller_ != nullptr; }

  std::unique_ptr<EventEngine::Endpoint> Connect() {
    int client_fd = ConnectToServerOrDie(address_);
    EventHandle* handle =
        poller_->CreateHandle(client_fd, "test", poller_->CanTrackErrors());
    CHECK_NE(handle, nullptr);
    ChannelArgsEndpointConfig config(BuildChannelArgs());
    PosixTcpOptions options = TcpOptionsFromEndpointConfig(config);
    return CreatePosixEndpoint(
        handle,
        PosixEngineClosure::TestOnlyToClosure(
            [poller = poller_](const absl::Status& /*status*/) {
              poller->Kick();
            }),
        event_engine_,
        grpc_core::ResourceQuota::Default()
            ->memory_quota()
            ->CreateMemoryAllocator("test"),
        options);
  }

  absl::Status WaitForListenerShutdown() {
    grpc_core::MutexLock lock(&mu_);
    while (!listener_shutdown_status_.has_value()) {
      cond_.Wait(&mu_);
    }
    return *listener_shutdown_status_;
  }

  std::unique_ptr<Endpoint> GetServerEndpoint(
      absl::Duration timeout = absl::Seconds(15)) {
    auto end_time = absl::Now() + timeout;
    absl::MutexLock lock(&mu_);
    while (server_endpoint_ == nullptr && absl::Now() < end_time) {
      cond_.WaitWithDeadline(&mu_, end_time);
    }
    return std::move(server_endpoint_);
  }

  std::shared_ptr<PosixEventEngine> event_engine() { return event_engine_; }

  std::shared_ptr<PosixEventPoller> poller() { return poller_; }

 private:
  grpc_core::ChannelArgs BuildChannelArgs() {
    grpc_core::ChannelArgs args;
    auto quota = grpc_core::ResourceQuota::Default();
    return args.Set(GRPC_ARG_RESOURCE_QUOTA, quota);
  }

  std::unique_ptr<TestScheduler> scheduler_;
  std::shared_ptr<PosixEventPoller> poller_;
  std::shared_ptr<PosixEventEngine> event_engine_;
  std::unique_ptr<Listener> listener_;
  EventEngine::ResolvedAddress address_;
  grpc_core::Mutex mu_;
  grpc_core::CondVar cond_;
  absl::optional<absl::Status> listener_shutdown_status_ ABSL_GUARDED_BY(&mu_);
  std::unique_ptr<Endpoint> server_endpoint_ ABSL_GUARDED_BY(&mu_);
  Worker* worker_ = nullptr;
};

void ReaderThread(absl::string_view label, int fd) {
  absl::Cord cord;
  std::array<char, 200000> buffer;
  ssize_t r = -500;
  while ((r = read(fd, buffer.data(), buffer.size())) > 0) {
    cord.Append(absl::string_view(buffer.data(), r));
    absl::string_view view = cord.Flatten();
    for (ssize_t it = view.find('\n'); it >= 0; it = view.find('\n')) {
      std::cout << absl::StrFormat("[ %s ] %s\n", label, view.substr(0, it));
      view = view.substr(it + 1);
    }
    cord = view;
  }
  std::cout << absl::StrFormat("[ %s ] Done\n", label);
}

}  // namespace

int main(int argc, char* argv[]) {
  absl::ParseCommandLine(argc, argv);
  absl::InitializeLog();
  grpc_init();
  // Needs to happen after the scope exit and after all other variables gone
  auto cleanup = absl::MakeCleanup([] { grpc_shutdown(); });
  grpc_core::Fork::Enable(true);
  int port = grpc_pick_unused_port_or_die();
  std::string addr = absl::StrCat("localhost:", port);
  std::string target_addr = absl::StrCat(
      "ipv6:[::1]:", std::to_string(grpc_pick_unused_port_or_die()));
  auto resolved_addr = URIToResolvedAddress(target_addr);
  CHECK_OK(resolved_addr);
  EventEngineHolder holder(*resolved_addr);
  CHECK(holder.ok());
  auto client = holder.Connect();
  CHECK_NE(client.get(), nullptr);
  auto server_end = holder.GetServerEndpoint();
  CHECK_NE(server_end.get(), nullptr);
  LOG(INFO) << "Endpoints status: "
            << SendValidatePayload("Hello world", server_end.get(),
                                   client.get());
  int stdouts[2];
  PCHECK(pipe(stdouts) >= 0);
  int stderrs[2];
  PCHECK(pipe(stderrs) >= 0);
  pid_t pid = fork();
  CHECK_GE(pid, 0);
  if (pid == 0) {
    close(stdouts[0]);
    close(stderrs[0]);
    dup2(stdouts[1], 1);
    dup2(stderrs[1], 2);
    LOG(INFO) << "I am here!";
    LOG(INFO) << "Endpoints status in child: "
              << SendValidatePayload("Hello world in child", server_end.get(),
                                     client.get());
    close(stdouts[1]);
    close(stderrs[1]);
  } else {
    close(stdouts[1]);
    close(stderrs[1]);
    std::thread stdout_reader(ReaderThread, "child out", stdouts[0]);
    std::thread stderr_reader(ReaderThread, "child err", stderrs[0]);
    LOG(INFO) << "Endpoints status in parent: "
              << SendValidatePayload("Hello world in parent", server_end.get(),
                                     client.get());
    int status = 0;
    waitpid(pid, &status, 0);
    CHECK(WIFEXITED(status)) << status;
    LOG(INFO) << "Child finished with " << WEXITSTATUS(status);
    close(stdouts[0]);
    close(stderrs[0]);
    stdout_reader.join();
    stderr_reader.join();
  }
}