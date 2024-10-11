#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <chrono>
#include <cstdio>
#include <memory>
#include <queue>
#include <thread>

#include "absl/flags/parse.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"

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

std::pair<std::unique_ptr<Reader>, std::unique_ptr<Writer>> SetupPipe() {
  std::array<int, 2> fds;
  if (pipe(fds.data()) == 0) {
    return {std::make_unique<Reader>(fds[0]), std::make_unique<Writer>(fds[1])};
  } else {
    PLOG(FATAL) << "Unable to open pipe";
    return {nullptr, nullptr};
  }
}

class EventEngineHolder {
 public:
  EventEngineHolder(const EventEngine::ResolvedAddress& address)
      : address_(address) {
    scheduler_ = std::make_unique<TestScheduler>();
    poller_ = MakeDefaultPoller(scheduler_.get());
    event_engine_ = PosixEventEngine::MakeTestOnlyPosixEventEngine(poller_);
    scheduler_->ChangeCurrentEventEngine(event_engine_.get());
  }

  ~EventEngineHolder() {
    listener_.reset();
    WaitForSingleOwnerWithTimeout(std::move(event_engine_),
                                  std::chrono::seconds(30));
  }

  bool ok() const { return poller_ != nullptr; }

  std::unique_ptr<EventEngine::Endpoint> StartConnection() {
    int client_fd = ConnectToServerOrDie(address_);
    EventHandle* handle =
        poller_->CreateHandle(client_fd, "test", poller_->CanTrackErrors());
    CHECK_NE(handle, nullptr);
    grpc_core::ChannelArgs args;
    ChannelArgsEndpointConfig config(args);
    PosixTcpOptions options = TcpOptionsFromEndpointConfig(config);
    return CreatePosixEndpoint(
        handle,
        PosixEngineClosure::TestOnlyToClosure(
            [poller = poller_](absl::Status /*status*/) { poller->Kick(); }),
        event_engine_,
        options.resource_quota->memory_quota()->CreateMemoryAllocator("test"),
        options);
  }

  absl::Status Listen() {
    std::unique_ptr<EventEngine::Endpoint> server_endpoint;
    grpc_core::Notification* server_signal = new grpc_core::Notification();
    Listener::AcceptCallback accept_cb =
        [&server_endpoint, &server_signal](
            std::unique_ptr<Endpoint> ep,
            grpc_core::MemoryAllocator /*memory_allocator*/) {
          server_endpoint = std::move(ep);
          server_signal->Notify();
        };
    grpc_core::ChannelArgs args;
    ChannelArgsEndpointConfig config(args);
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
    if (!status.ok()) {
      return status;
    }
    status = listener_->Start();
    if (!status.ok()) {
      return status;
    }
    return absl::OkStatus();
  }

  absl::Status WaitForListenerShutdown() {
    grpc_core::MutexLock lock(&mu_);
    while (!listener_shutdown_status_.has_value()) {
      cond_.Wait(&mu_);
    }
    return *listener_shutdown_status_;
  }

 private:
  EventEngine::ResolvedAddress address_;
  std::unique_ptr<TestScheduler> scheduler_;
  std::shared_ptr<PosixEventPoller> poller_;
  std::shared_ptr<PosixEventEngine> event_engine_;
  std::unique_ptr<Listener> listener_;
  grpc_core::Mutex mu_;
  grpc_core::CondVar cond_;
  absl::optional<absl::Status> listener_shutdown_status_ ABSL_GUARDED_BY(&mu_);
  std::unique_ptr<Endpoint> server_endpoints_ ABSL_GUARDED_BY(&mu_);
};

}  // namespace

int main(int argc, char* argv[]) {
  absl::ParseCommandLine(argc, argv);
  absl::InitializeLog();
  grpc_core::Fork::Enable(true);
  int port = grpc_pick_unused_port_or_die();
  std::string addr = absl::StrCat("localhost:", port);
  auto reader_writer = SetupPipe();
  std::string target_addr = absl::StrCat(
      "ipv6:[::1]:", std::to_string(grpc_pick_unused_port_or_die()));
  auto resolved_addr = URIToResolvedAddress(target_addr);
  CHECK(resolved_addr.ok()) << resolved_addr.status();
  EventEngineHolder holder(*resolved_addr);
  CHECK(holder.ok());
  absl::Status status = holder.Listen();
  CHECK(status.ok()) << status;
  LOG(INFO) << "Done";

  // pid_t pid = fork();
  // if (pid < 0) {
  //   PLOG(FATAL) << "Call to fork failed";
  //   return -1;
  // } else if (pid == 0) {
  //   auto writer = std::move(reader_writer.second);
  //   Server server;
  //   reader_writer = {};
  //   writer->Write("Child message");
  //   return 1;
  // } else {
  //   auto reader = std::move(reader_writer.first);
  //   reader_writer = {};
  //   std::cout << "Child " << pid << " said " << reader->Read() << "\n";
  //   int status;
  //   do {
  //     waitpid(pid, &status, 0);
  //   } while (!WIFEXITED(status));
  //   return WEXITSTATUS(status);
  // }
}