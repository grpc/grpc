// Copyright 2024 gRPC Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "fork_test_utils.h"

#include <netinet/in.h>
#include <sched.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <thread>

#include "absl/strings/cord.h"
#include "absl/strings/str_cat.h"

#include "include/grpc/event_engine/event_engine.h"
#include <grpc/fork.h>

#include "src/core/lib/event_engine/channel_args_endpoint_config.h"
#include "src/core/lib/event_engine/posix_engine/event_poller_posix_default.h"
#include "src/core/lib/event_engine/posix_engine/posix_endpoint.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "test/core/event_engine/event_engine_test_utils.h"
#include "test/core/event_engine/posix/posix_engine_test_utils.h"
#include "test/core/test_util/port.h"

namespace event_engine {
namespace experimental {
namespace testing {

using namespace ::grpc_event_engine::experimental;
using Endpoint = ::grpc_event_engine::experimental::EventEngine::Endpoint;
using Listener = ::grpc_event_engine::experimental::EventEngine::Listener;
using namespace std::chrono_literals;

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

//
// EventEngineHolder
//
EventEngineHolder::EventEngineHolder() {
  std::string target_addr = absl::StrCat(
      "ipv6:[::1]:", std::to_string(grpc_pick_unused_port_or_die()));
  auto resolved_addr = URIToResolvedAddress(target_addr);
  CHECK_OK(resolved_addr);
  address_ = *resolved_addr;
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

EventEngineHolder::~EventEngineHolder() {
  if (worker_ != nullptr) {
    worker_->Wait();
  }
  listener_.reset();
  WaitForSingleOwnerWithTimeout(std::move(event_engine_),
                                std::chrono::seconds(30));
}

std::unique_ptr<EventEngine::Endpoint> EventEngineHolder::Connect() {
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

//
// ChildMonitor
//
ChildMonitor::ChildMonitor(pid_t pid, int fd_stdout, int fd_stderr)
    : pid_(pid),
      stdout_thread_(MonitorFd, absl::StrCat(pid_, " out"), fd_stdout, this),
      stderr_thread_(MonitorFd, absl::StrCat(pid_), fd_stderr, this) {
  grpc_core::MutexLock lock(&mu_);
  while (started_count_ < 2) {
    cond_.Wait(&mu_);
  }
}

ChildMonitor::~ChildMonitor() {
  {
    grpc_core::MutexLock lock(&mu_);
    while (stopped_count_ < 2) {
      cond_.Wait(&mu_);
    }
  }
  // Wait on child
  (void)ChildStatus();
  stdout_thread_.join();
  stderr_thread_.join();
}

absl::Status ChildMonitor::ChildStatus() {
  grpc_core::MutexLock lock(&mu_);
  if (!child_status_.has_value()) {
    int status = 0;
    waitpid(pid_, &status, 0);
    child_status_ = ProcessChildStatus(status);
  }
  return *child_status_;
}

void ChildMonitor::MonitorFd(absl::string_view label, int fd,
                             ChildMonitor* monitor) {
  monitor->ReportThreadStarted();
  absl::Cord cord;
  std::array<char, 200000> buffer;
  ssize_t r = -500;
  while ((r = read(fd, buffer.data(), buffer.size())) > 0) {
    cord.Append(absl::string_view(buffer.data(), r));
    absl::string_view view = cord.Flatten();
    for (ssize_t it = view.find('\n'); it >= 0; it = view.find('\n')) {
      std::cout << absl::StrFormat("\e[38;2;255;165;0m[ %s ] %s\033[0m\n",
                                   label, view.substr(0, it));
      view = view.substr(it + 1);
    }
    cord = view;
  }
  close(fd);
  if (!cord.empty()) {
    std::cout << absl::StrFormat("[ %s ] %s\n", label, cord);
  }
  monitor->ReportThreadDone();
}

absl::Status ChildMonitor::ProcessChildStatus(int status) {
  if (WIFEXITED(status)) {
    int exit_status = WEXITSTATUS(status);
    if (exit_status == 0) {
      return absl::OkStatus();
    }
    return absl::UnknownError(
        absl::StrFormat("Child %ld exited with status %d", pid_, exit_status));
  } else if (WIFSIGNALED(status)) {
    return absl::UnknownError(
        absl::StrFormat("Child %ld terminated with signal %s", pid_,
                        strsignal(WTERMSIG(status))));
  }
  return absl::UnknownError(absl::StrFormat(
      "Stopped: %v, continued: %v", WIFSTOPPED(status), WIFCONTINUED(status)));
}

int ChildMonitor::ReportThreadDone() {
  grpc_core::MutexLock lock(&mu_);
  stopped_count_++;
  cond_.SignalAll();
  return stopped_count_;
}

int ChildMonitor::ReportThreadStarted() {
  grpc_core::MutexLock lock(&mu_);
  started_count_++;
  cond_.SignalAll();
  return started_count_;
}

}  // namespace testing
}  // namespace experimental
}  // namespace event_engine