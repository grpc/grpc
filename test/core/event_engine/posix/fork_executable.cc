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

#include <netinet/in.h>
#include <sched.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <thread>
#include <utility>

#include "absl/cleanup/cleanup.h"
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/time/clock.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/fork.h>

#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/gprpp/fork.h"
#include "test/core/event_engine/event_engine_test_utils.h"
#include "test/core/event_engine/posix/fork_test_utils.h"
#include "test/core/test_util/port.h"

ABSL_FLAG(bool, child_pause, false,
          "Pause child and wait for \"1\" entered on stdin");

namespace {
using namespace ::grpc_event_engine::experimental;
using Endpoint = ::grpc_event_engine::experimental::EventEngine::Endpoint;

class FdCloser {
 public:
  explicit FdCloser(int out_fd, int err_fd) : fds_({out_fd, err_fd}) {}

  FdCloser(const FdCloser& other) = delete;

  ~FdCloser() {
    for (int fd : fds_) {
      close(fd);
    }
  }

  int stdout() const { return fds_[0]; }
  int stderr() const { return fds_[1]; }

 private:
  std::vector<int> fds_;
};

int ChildProcessMain(std::unique_ptr<FdCloser> fds,
                     std::unique_ptr<Endpoint> client,
                     std::unique_ptr<Endpoint> server_end) {
  dup2(fds->stdout(), 1);
  dup2(fds->stderr(), 2);
  if (absl::GetFlag(FLAGS_child_pause)) {
    volatile bool flag = false;
    while (!flag) {
      LOG_EVERY_N_SEC(INFO, 15) << "Flip the value in debugger";
    }
    LOG(INFO) << "Resuming!";
  }
  LOG(INFO) << "Child process is running";
  LOG(INFO) << "Endpoints status in child: "
            << SendValidatePayload("Hello world in child", server_end.get(),
                                   client.get());
  return 0;
}

class ParentProcessMonitor {
 public:
  ParentProcessMonitor() : bg_thread_(ThreadLoop, &done_) {}

  ~ParentProcessMonitor() {
    done_.store(true);
    bg_thread_.join();
    LOG(INFO) << absl::StrFormat("Parent %ld is done", getpid());
  }

 private:
  static void ThreadLoop(std::atomic_bool* done) {
    pid_t pid = getpid();
    auto start_time = absl::Now();
    while (!done->load(std::memory_order_relaxed)) {
      LOG_EVERY_N_SEC(INFO, 5)
          << absl::StrFormat("Parent process %ld had been running for %v", pid,
                             absl::Now() - start_time);
      absl::SleepFor(absl::Milliseconds(300));
    }
  }

  std::atomic_bool done_{false};
  std::thread bg_thread_;
};

absl::Status ParentProcessMain(std::unique_ptr<FdCloser> fds, pid_t child_pid,
                               std::unique_ptr<Endpoint> client,
                               std::unique_ptr<Endpoint> server_end) {
  ParentProcessMonitor self_monitor;  // Will remind that the parent is alive
  event_engine::experimental::testing::ChildMonitor monitor(
      child_pid, fds->stdout(), fds->stderr());
  // LOG(INFO) << "Endpoints status in parent: "
  //           << SendValidatePayload("Hello world in parent", server_end.get(),
  //                                  client.get());
  return monitor.ChildStatus();
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
  event_engine::experimental::testing::EventEngineHolder holder(*resolved_addr);
  CHECK(holder.ok());
  auto client = holder.Connect();
  CHECK_NE(client.get(), nullptr);
  auto server_end = holder.GetServerEndpoint();
  CHECK_NE(server_end.get(), nullptr);
  LOG(INFO) << "Endpoints status: "
            << SendValidatePayload("Hello world", server_end.get(),
                                   client.get());
  std::array<int, 2> stdouts;
  PCHECK(pipe(stdouts.data()) >= 0);
  std::array<int, 2> stderrs;
  PCHECK(pipe(stderrs.data()) >= 0);
  auto parent_process_fds = std::make_unique<FdCloser>(stdouts[0], stderrs[0]);
  auto child_process_fds = std::make_unique<FdCloser>(stdouts[1], stderrs[1]);
  pid_t pid = fork();
  CHECK_GE(pid, 0);
  if (pid == 0) {
    parent_process_fds.reset();
    return ChildProcessMain(std::move(child_process_fds), std::move(client),
                            std::move(server_end));
  } else {
    child_process_fds.reset();
    absl::Status child_status =
        ParentProcessMain(std::move(parent_process_fds), pid, std::move(client),
                          std::move(server_end));
    QCHECK_OK(child_status);
    return 0;
  }
}