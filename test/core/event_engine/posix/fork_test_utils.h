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

#ifndef GRPC_TEST_CORE_EVENT_ENGINE_POSIX_FORK_TEST_UTILS_H
#define GRPC_TEST_CORE_EVENT_ENGINE_POSIX_FORK_TEST_UTILS_H

#include <netinet/in.h>
#include <sched.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <thread>

#include <grpc/event_engine/event_engine.h>
#include <grpc/fork.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/event_engine/posix_engine/event_poller_posix_default.h"
#include "src/core/lib/event_engine/posix_engine/posix_engine.h"
#include "test/core/event_engine/posix/posix_engine_test_utils.h"

namespace event_engine {
namespace experimental {
namespace testing {

class Worker;

class EventEngineHolder {
 private:
  using ResolvedAddress =
      ::grpc_event_engine::experimental::EventEngine::ResolvedAddress;
  using Endpoint = ::grpc_event_engine::experimental::EventEngine::Endpoint;
  using Listener = ::grpc_event_engine::experimental::EventEngine::Listener;
  using PosixEventEngine = ::grpc_event_engine::experimental::PosixEventEngine;
  using PosixEventPoller = ::grpc_event_engine::experimental::PosixEventPoller;
  using TestScheduler = ::grpc_event_engine::experimental::TestScheduler;

 public:
  EventEngineHolder(const ResolvedAddress& address);
  ~EventEngineHolder();
  std::unique_ptr<Endpoint> Connect();

  bool ok() const { return poller_ != nullptr; }

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
  ResolvedAddress address_;
  grpc_core::Mutex mu_;
  grpc_core::CondVar cond_;
  absl::optional<absl::Status> listener_shutdown_status_ ABSL_GUARDED_BY(&mu_);
  std::unique_ptr<Endpoint> server_endpoint_ ABSL_GUARDED_BY(&mu_);
  Worker* worker_ = nullptr;
};

class ChildMonitor {
 public:
  ChildMonitor(pid_t pid, int fd_stdout, int fd_stderr);
  ~ChildMonitor();

  absl::Status ChildStatus();

 private:
  static void MonitorFd(absl::string_view label, int fd, ChildMonitor* monitor);

  int ReportThreadStarted();
  int ReportThreadDone();
  absl::Status ProcessChildStatus(int status);

  pid_t pid_;
  grpc_core::Mutex mu_;
  grpc_core::CondVar cond_;
  int started_count_ ABSL_GUARDED_BY(mu_) = 0;
  int stopped_count_ ABSL_GUARDED_BY(mu_) = 0;
  absl::optional<absl::Status> child_status_ ABSL_GUARDED_BY(mu_);
  std::thread stdout_thread_;
  std::thread stderr_thread_;
};

}  // namespace testing
}  // namespace experimental
}  // namespace event_engine

#endif  // GRPC_TEST_CORE_EVENT_ENGINE_POSIX_FORK_TEST_UTILS_H