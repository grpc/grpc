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
#include <memory>
#include <thread>

#include "absl/cleanup/cleanup.h"
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "absl/time/clock.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/fork.h>

#include "src/core/lib/gprpp/fork.h"
#include "test/core/event_engine/event_engine_test_utils.h"
#include "test/core/event_engine/posix/fork_test_utils.h"

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

class ReadWriteResult {
 public:
  void ReadDone(absl::StatusOr<std::string> result) {
    absl::MutexLock lock(&mu_);
    read_result_ = std::move(result);
    cond_.SignalAll();
  }

  void WriteDone(absl::Status status) {
    absl::MutexLock lock(&mu_);
    write_result_ = std::move(status);
    cond_.SignalAll();
  }

  std::pair<absl::Status, absl::StatusOr<std::string>> WaitForResult() {
    absl::MutexLock lock_(&mu_);
    while (!read_result_.has_value() || !write_result_.has_value()) {
      cond_.Wait(&mu_);
    }
    return {*write_result_, *read_result_};
  }

 private:
  grpc_core::Mutex mu_;
  absl::CondVar cond_;
  absl::optional<absl::StatusOr<std::string>> read_result_
      ABSL_GUARDED_BY(&mu_);
  absl::optional<absl::Status> write_result_ ABSL_GUARDED_BY(&mu_);
};

std::pair<absl::Status, absl::StatusOr<std::string>> SendValidatePayload2(
    absl::string_view data, EventEngine::Endpoint* send_endpoint,
    EventEngine::Endpoint* receive_endpoint) {
  CHECK_NE(receive_endpoint, nullptr);
  CHECK_NE(send_endpoint, nullptr);
  int num_bytes_written = data.size();
  SliceBuffer read_slice_buf;
  SliceBuffer read_store_buf;
  SliceBuffer write_slice_buf;

  ReadWriteResult result;

  read_slice_buf.Clear();
  write_slice_buf.Clear();
  read_store_buf.Clear();

  AppendStringToSliceBuffer(&write_slice_buf, data);
  EventEngine::Endpoint::ReadArgs args = {num_bytes_written};
  std::function<void(absl::Status)> read_cb;
  read_cb = [receive_endpoint, &read_slice_buf, &read_store_buf, &read_cb,
             &args, &result](absl::Status status) {
    if (!status.ok()) {
      result.ReadDone(status);
      return;
    }
    if (read_slice_buf.Length() == static_cast<size_t>(args.read_hint_bytes)) {
      read_slice_buf.MoveFirstNBytesIntoSliceBuffer(read_slice_buf.Length(),
                                                    read_store_buf);
      result.ReadDone(ExtractSliceBufferIntoString(&read_store_buf));
      return;
    }
    args.read_hint_bytes -= read_slice_buf.Length();
    read_slice_buf.MoveFirstNBytesIntoSliceBuffer(read_slice_buf.Length(),
                                                  read_store_buf);
    if (receive_endpoint->Read(read_cb, &read_slice_buf, &args)) {
      CHECK_NE(read_slice_buf.Length(), 0u);
      read_cb(absl::OkStatus());
    }
  };
  // Start asynchronous reading at the receive_endpoint.
  if (receive_endpoint->Read(read_cb, &read_slice_buf, &args)) {
    read_cb(absl::OkStatus());
  }
  // Start asynchronous writing at the send_endpoint.
  if (send_endpoint->Write(
          [&result](absl::Status status) {
            result.WriteDone(std::move(status));
          },
          &write_slice_buf, nullptr)) {
    result.WriteDone(absl::OkStatus());
  }
  return result.WaitForResult();
}

int ChildProcessMain(std::unique_ptr<FdCloser> fds,
                     std::unique_ptr<Endpoint> client,
                     std::unique_ptr<Endpoint> server_end) {
  dup2(fds->stdout(), 1);
  dup2(fds->stderr(), 2);
  if (absl::GetFlag(FLAGS_child_pause)) {
    // Volatile so compiler does not optimize this out.
    // Attach GDB, switch to this frame and do `set flag=1` and `continue` to
    // resume the child.
    volatile bool flag = false;
    while (!flag) {
      LOG_EVERY_N_SEC(INFO, 5) << "Flip the value in debugger";
    }
    LOG(INFO) << "Resuming!";
  }
  LOG(INFO) << absl::StrFormat("Child process %ld is running", getpid());
  auto doomed_send = SendValidatePayload2("Hello world in child",
                                          server_end.get(), client.get());
  CHECK_EQ(doomed_send.second.status().code(), absl::StatusCode::kInternal)
      << doomed_send.second.status();
  return 0;
}

class ParentProcessMonitor {
 public:
  ParentProcessMonitor() : bg_thread_(ThreadLoop, &done_) {}

  ~ParentProcessMonitor() {
    done_.store(true);
    bg_thread_.join();
  }

 private:
  static void ThreadLoop(std::atomic_bool* done) {
    pid_t pid = getpid();
    auto start_time = absl::Now();
    while (!done->load(std::memory_order_relaxed)) {
      LOG_EVERY_N_SEC(INFO, 10)
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
  event_engine::experimental::testing::EventEngineHolder holder;
  CHECK(holder.ok());
  auto client = holder.Connect();
  CHECK_NE(client.get(), nullptr);
  auto server_end = holder.GetServerEndpoint();
  CHECK_NE(server_end.get(), nullptr);
  auto result =
      SendValidatePayload2("Hello world", server_end.get(), client.get());
  CHECK_OK(result.first);
  CHECK_OK(result.second);
  CHECK_EQ(*result.second, "Hello world");
  LOG(INFO) << "Endpoint works";
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
    absl::Status child_process_status =
        ParentProcessMain(std::move(parent_process_fds), pid, std::move(client),
                          std::move(server_end));
    QCHECK_OK(child_process_status);
    LOG(INFO) << absl::StrFormat("Parent %ld is done", getpid());
    return 0;
  }
}