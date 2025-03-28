// Copyright 2025 gRPC Authors
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

#include <arpa/inet.h>
#include <fcntl.h>
#include <gmock/gmock.h>
#include <grpc/event_engine/event_engine.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/support/status.h>
#include <gtest/gtest.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <memory>
#include <optional>
#include <queue>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "absl/cleanup/cleanup.h"
#include "absl/debugging/stacktrace.h"
#include "absl/debugging/symbolize.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/substitute.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/event_engine/channel_args_endpoint_config.h"
#include "src/core/lib/event_engine/posix_engine/posix_engine.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/util/wait_for_single_owner.h"
#include "test/core/test_util/port.h"
#include "test/core/test_util/test_config.h"

namespace grpc_event_engine::experimental {

std::string GetBacktrace() {
  std::array<void*, 10> ptrs;
  int frames = absl::GetStackTrace(ptrs.data(), ptrs.size(), 1);
  std::vector<std::string> bt(frames);
  for (int i = 0; i < frames; ++i) {
    std::array<char, 150> frame{0};
    std::string frame_str = absl::Symbolize(ptrs[i], frame.data(), frame.size())
                                ? std::string(frame.data(), frame.size())
                                : "<Boop>";
    bt[i] = absl::Substitute("  $0 $1", i, frame_str);
  }
  return absl::StrJoin(bt, "\n");
}

namespace {

thread_local bool is_in_scheduler = false;

MATCHER(IsOk, "is ok") { return arg.ok(); }

MATCHER_P(StatusIs, status, "") {
  *result_listener << "where the status is " << status;
  return arg.code() == status;
}

class StatusListener {
 public:
  absl::Status AwaitStatus() {
    grpc_core::MutexLock lock(&mu_);
    while (statuses_.empty()) {
      cond_.Wait(&mu_);
    }
    absl::Status status = std::move(statuses_).back();
    statuses_.pop_back();
    for (const auto& s : statuses_) {
      LOG(INFO) << "Another status: " << s;
    }
    return status;
  }

  absl::AnyInvocable<void(absl::Status)> Setter() {
    return [&](absl::Status status) {
      grpc_core::MutexLock lock(&mu_);
      statuses_.emplace_back(std::move(status));
      cond_.SignalAll();
    };
  }

 private:
  grpc_core::Mutex mu_;
  grpc_core::CondVar cond_;
  std::vector<absl::Status> statuses_ ABSL_GUARDED_BY(&mu_);
};

class RawPosixClient {
 public:
  explicit RawPosixClient(const EventEngine::ResolvedAddress& address) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
      status_ = absl::ErrnoToStatus(errno, "socket call");
    } else {
      int result =
          connect(sockfd, address.address(), sizeof(*address.address()));
      if (result < 0) {
        status_ = absl::ErrnoToStatus(errno, "connect call");
      } else {
        socket_ = sockfd;
        status_ = absl::OkStatus();
      }
    }
  }

  RawPosixClient(const RawPosixClient& /* other */) = delete;

  RawPosixClient(RawPosixClient&& other) noexcept
      : socket_(other.socket_), status_(other.status()) {
    other.socket_ = -1;
    other.status_ = absl::OkStatus();
  }

  ~RawPosixClient() {
    if (socket_ > 0) {
      close(socket_);
      socket_ = -1;
    }
  }

  absl::Status status() const& { return status_; }

  absl::Status&& status() && { return std::move(status_); }

  int socket_fd() const { return socket_; }

  absl::Status Read(size_t bytes) const {
    if (!status_.ok()) {
      return status_;
    }
    std::vector<std::byte> result;
    while (result.size() < bytes) {
      std::vector<std::byte> buffer(1024 * 1024, static_cast<std::byte>(42));
      auto r = read(socket_, buffer.data(), buffer.size());
      if (r < 0) {
        return absl::ErrnoToStatus(errno, "Socket read");
      } else if (r == 0) {
        return absl::FailedPreconditionError(absl::StrFormat(
            "Read %ld bytes, expected %ld", result.size(), bytes));
      }
      std::copy(buffer.begin(), buffer.begin() + r, std::back_inserter(result));
    }
    return absl::OkStatus();
  }

  absl::Status Write(absl::string_view data) {
    if (!status_.ok()) {
      return status_;
    }
    auto written = write(socket_, data.data(), data.size());
    if (written < 0) {
      return absl::ErrnoToStatus(errno, "Socket write");
    } else if (written == 0) {
      return absl::DataLossError("EOF");
    } else if (written < data.size()) {
      return absl::ResourceExhaustedError(
          absl::StrFormat("%ld bytes sent, out of %ld", written, data.size()));
    }
    return absl::OkStatus();
  }

 private:
  int socket_ = -1;
  absl::Status status_;
};

class PollerForkTest : public ::testing::Test {
 public:
  void SetUp() override {
    ee_ = GetDefaultEventEngine();
    // absl::StrSplit(grpc_core::ConfigVars::Get().PollStrategy(), ',')
    // Setup listener and establish socket connection, confirm they work
    auto listener_and_address = SetupListener(
        [&](auto endpoint, MemoryAllocator /* memory */) {
          grpc_core::MutexLock lock(&mu_);
          endpoints_.emplace(std::move(endpoint));
          cond_.SignalAll();
        },
        listener_done_.Setter());
    ASSERT_THAT(listener_and_address, IsOk());
    address_ = listener_and_address->second;
    listener_ = std::move(listener_and_address->first);
    RawPosixClient client(listener_and_address->second);
    ASSERT_THAT(client.status(), IsOk());
    // Sanity check - confirm a read operation works
    ASSERT_THAT(SendFromRawToEE(client.socket_fd(), *AwaitEndpoint(), "Hello"),
                IsOk());
  }

  void TearDown() override {
    {
      grpc_core::MutexLock lock(&mu_);
      EXPECT_THAT(endpoints_, ::testing::IsEmpty());
      endpoints_ = {};
    }
    listener_.reset();
    EXPECT_THAT(listener_done_.AwaitStatus(), IsOk());
    grpc_core::WaitForSingleOwnerWithTimeout(std::move(ee_),
                                             grpc_core::Duration::Seconds(30));
  }

  std::unique_ptr<EventEngine::Endpoint> AwaitEndpoint() {
    grpc_core::MutexLock lock(&mu_);
    while (endpoints_.empty()) {
      cond_.Wait(&mu_);
    }
    std::unique_ptr<EventEngine::Endpoint> endpoint =
        std::move(endpoints_.back());
    endpoints_.pop();
    LOG(INFO) << "Endpoint connected: "
              << ResolvedAddressToNormalizedString(endpoint->GetPeerAddress());
    return endpoint;
  }

  PosixEventEngine* ee() {
    return std::static_pointer_cast<PosixEventEngine>(ee_).get();
  }

  absl::StatusOr<std::pair<std::unique_ptr<EventEngine::Listener>,
                           EventEngine::ResolvedAddress>>
  SetupListener(absl::AnyInvocable<void(std::unique_ptr<EventEngine::Endpoint>,
                                        MemoryAllocator)>
                    on_accept,
                absl::AnyInvocable<void(absl::Status)> on_shutdown) {
    int port = grpc_pick_unused_port_or_die();
    grpc_core::ChannelArgs args;
    auto quota = grpc_core::ResourceQuota::Default();
    args = args.Set(GRPC_ARG_RESOURCE_QUOTA, quota);
    ChannelArgsEndpointConfig config(args);
    std::optional<absl::Status> listener_done;
    std::vector<std::unique_ptr<EventEngine::Endpoint>> endpoints;
    auto listener = ee()->CreateListener(
        std::move(on_accept), std::move(on_shutdown), config,
        std::make_unique<grpc_core::MemoryQuota>("foo"));
    if (!listener.ok()) {
      return std::move(listener).status();
    }
    auto address = URIToResolvedAddress(absl::StrCat("ipv4:127.0.0.1:", port));
    if (!address.ok()) {
      return std::move(address).status();
    }
    auto bound_port = (*listener)->Bind(*address);
    if (!bound_port.ok()) {
      return std::move(bound_port).status();
    }
    auto status = (*listener)->Start();
    if (!status.ok()) {
      return status;
    }
    return std::make_pair(std::move(listener).value(),
                          std::move(address).value());
  }

  absl::Status SendFromRawToEE(int socket_fd, EventEngine::Endpoint& endpoint,
                               absl::string_view data) {
    SliceBuffer buffer;
    StatusListener read_status;
    EventEngine::Endpoint::ReadArgs read_args = {
        static_cast<int64_t>(data.length())};
    if (endpoint.Read(read_status.Setter(), &buffer, &read_args)) {
      return absl::FailedPreconditionError("Endpoint has pending data");
    }
    ssize_t wrote = write(socket_fd, data.data(), data.size());
    if (wrote < 0) {
      return absl::ErrnoToStatus(errno, "Write to socket");
    }
    if (wrote < data.size()) {
      return absl::DataLossError("Did not write all the data");
    }
    auto status = read_status.AwaitStatus();
    if (!status.ok()) {
      return status;
    }
    if (buffer.Length() != data.size()) {
      return absl::InternalError(absl::StrFormat("Read %ld instead of %ld",
                                                 buffer.Length(), data.size()));
    }
    Slice slice = buffer.TakeFirst();
    if (slice.as_string_view() != data) {
      return absl::InternalError(absl::StrFormat("Read %v, expected %v",
                                                 slice.as_string_view(), data));
    }
    LOG(INFO) << "Read " << slice.as_string_view();
    return absl::OkStatus();
  }

 protected:
  std::shared_ptr<EventEngine> ee_;
  grpc_core::Mutex mu_;
  grpc_core::CondVar cond_;
  StatusListener listener_done_;
  std::unique_ptr<EventEngine::Listener> listener_;
  std::queue<std::unique_ptr<EventEngine::Endpoint>> endpoints_
      ABSL_GUARDED_BY(&mu_);
  EventEngine::ResolvedAddress address_;
};

}  // namespace

TEST_F(PollerForkTest, ListenerInParent) {
  // Connect before "fork"
  RawPosixClient client(address_);
  ASSERT_THAT(client.status(), IsOk());
  auto endpoint = AwaitEndpoint();
  // Start read and write, cause the fork. Both operations should succeed
  // post-fork.
  StatusListener read_status;
  StatusListener write_status;
  SliceBuffer read_buffer;
  SliceBuffer write_buffer;
  // 4M seems to be enough to fill the buffers on my Linux instance. May need to
  // be adjusted in the future!
  MutableSlice slice = MutableSlice::CreateUninitialized(4 * 1024 * 1024);
  std::fill(slice.begin(), slice.end(), 42);
  write_buffer.Append(Slice(std::move(slice)));
  ASSERT_FALSE(endpoint->Read(read_status.Setter(), &read_buffer, nullptr));
  ASSERT_FALSE(endpoint->Write(write_status.Setter(), &write_buffer, nullptr))
      << "Need to send more data";
  LOG(INFO) << "Before fork in parent";
  // Let the data reach the buffers
  absl::SleepFor(absl::Milliseconds(50 * grpc_test_slowdown_factor()));
  ee()->fork_support_for_tests()->BeforeFork();
  ee()->fork_support_for_tests()->AfterFork(false);
  LOG(INFO) << "After fork in parent";
  ASSERT_THAT(client.Read(write_buffer.Length()), IsOk());
  ASSERT_THAT(client.Write("Hi!"), IsOk());
  EXPECT_THAT(read_status.AwaitStatus(), IsOk());
  EXPECT_THAT(write_status.AwaitStatus(), IsOk());
  // Starting read and write post-fork will fail asynchronously and return the
  // status.
  ASSERT_FALSE(endpoint->Read(read_status.Setter(), &read_buffer, nullptr));
  ASSERT_THAT(client.Write("Hi again"), IsOk());
  EXPECT_THAT(read_status.AwaitStatus(), IsOk());
  bool write_result =
      endpoint->Write(write_status.Setter(), &write_buffer, nullptr);
  ASSERT_THAT(client.Read(write_buffer.Length()), IsOk());
  if (!write_result) {
    EXPECT_THAT(write_status.AwaitStatus(),
                StatusIs(absl::StatusCode::kInternal));
  }
}

TEST_F(PollerForkTest, ListenerInChild) {
  // Connect before "fork"
  RawPosixClient client(address_);
  ASSERT_THAT(client.status(), IsOk());
  auto endpoint = AwaitEndpoint();
  // Start read and write
  StatusListener read_status;
  StatusListener write_status;
  SliceBuffer read_buffer;
  SliceBuffer write_buffer;
  // 4M seems to be enough to fill the buffers on my Linux instance. May need to
  // be adjusted in the future!
  MutableSlice slice = MutableSlice::CreateUninitialized(4 * 1024 * 1024);
  std::fill(slice.begin(), slice.end(), 42);
  write_buffer.Append(Slice(std::move(slice)));
  ASSERT_FALSE(endpoint->Read(read_status.Setter(), &read_buffer, nullptr));
  ASSERT_FALSE(endpoint->Write(write_status.Setter(), &write_buffer, nullptr))
      << "Need to send more data";
  LOG(INFO) << "Before fork in child";
  // Let the data reach the buffers
  absl::SleepFor(absl::Milliseconds(50 * grpc_test_slowdown_factor()));
  ee()->fork_support_for_tests()->BeforeFork();
  ee()->fork_support_for_tests()->AfterFork(true);
  LOG(INFO) << "After fork in child";
  EXPECT_THAT(read_status.AwaitStatus(),
              StatusIs(absl::StatusCode::kResourceExhausted));
  EXPECT_THAT(write_status.AwaitStatus(),
              StatusIs(absl::StatusCode::kResourceExhausted));
  // Starting read and write post-fork will fail asynchronously and return the
  // status.
  ASSERT_FALSE(endpoint->Read(read_status.Setter(), &read_buffer, nullptr));
  ASSERT_FALSE(endpoint->Write(write_status.Setter(), &write_buffer, nullptr));
  EXPECT_THAT(read_status.AwaitStatus(), StatusIs(absl::StatusCode::kInternal));
  EXPECT_THAT(write_status.AwaitStatus(), StatusIs(absl::StatusCode::kUnknown));
}

class TestScheduler {
 public:
  static std::shared_ptr<TestScheduler> Start() {
    auto scheduler = std::make_shared<TestScheduler>();
    scheduler->Start(scheduler);
    return scheduler;
  }

  ~TestScheduler() {
    {
      grpc_core::MutexLock lock(&mu_);
      done_ = true;
      cond_.SignalAll();
    }
    if (background_thread_.joinable()) {
      background_thread_.join();
    }
  }

  void Schedule(absl::AnyInvocable<void() const> op)
      ABSL_NO_THREAD_SAFETY_ANALYSIS {
    if (is_in_scheduler) {
      pending_.push(std::move(op));
    } else {
      grpc_core::MutexLock lock(&mu_);
      pending_.push(std::move(op));
    }
    cond_.SignalAll();
  }

 private:
  void Start(std::shared_ptr<TestScheduler> scheduler) {
    background_thread_ = std::thread(
        [scheduler = std::move(scheduler)]() { scheduler->EventQueue(); });
  }

  void EventQueue() {
    is_in_scheduler = true;
    absl::Cleanup reset_is_in_scheduler = []() { is_in_scheduler = false; };
    grpc_core::MutexLock lock(&mu_);
    while (!done_) {
      while (!pending_.empty()) {
        pending_.front()();
        pending_.pop();
      }
      cond_.Wait(&mu_);
    }
  }

  std::thread background_thread_;
  grpc_core::Mutex mu_;
  grpc_core::CondVar cond_;
  std::queue<absl::AnyInvocable<void() const>> pending_ ABSL_GUARDED_BY(&mu_);
  bool done_ ABSL_GUARDED_BY(&mu_) = false;
};

class PollCycle {
 public:
  explicit PollCycle(std::shared_ptr<TestScheduler> scheduler,
                     absl::AnyInvocable<void() const> op)
      : scheduler_(std::move(scheduler)), op_(std::move(op)), is_scheduled_(1) {
    scheduler_->Schedule([this]() { Op(); });
  }

  ~PollCycle() {
    grpc_core::MutexLock lock(&mu_);
    done_ = true;
    while (is_scheduled_ > 0) {
      cond_.Wait(&mu_);
    }
  }

 private:
  void Op() {
    grpc_core::MutexLock lock(&mu_);
    --is_scheduled_;
    CHECK_EQ(is_scheduled_, 0);
    op_();
    if (!done_) {
      scheduler_->Schedule([this]() { Op(); });
      ++is_scheduled_;
    }
    cond_.SignalAll();
  }

  std::shared_ptr<TestScheduler> scheduler_;
  absl::AnyInvocable<void() const> op_;
  grpc_core::Mutex mu_;
  bool done_ ABSL_GUARDED_BY(&mu_) = false;
  int is_scheduled_ ABSL_GUARDED_BY(&mu_) = 0;
  grpc_core::CondVar cond_;
};

TEST(ExperimentalAsyncPollTest, AsyncPollTest) {
  auto scheduler = TestScheduler::Start();
  grpc_core::Mutex op_mu;
  grpc_core::CondVar cond;
  int runs = 0;

  auto cycle = std::make_unique<PollCycle>(scheduler, [&]() {
    grpc_core::MutexLock lock(&op_mu);
    runs++;
    cond.SignalAll();
  });
  {
    grpc_core::MutexLock lock(&op_mu);
    while (runs < 5) {
      cond.Wait(&op_mu);
    }
  }
  cycle.reset();
}

}  // namespace grpc_event_engine::experimental

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  int r = RUN_ALL_TESTS();
  grpc_shutdown();
  return r;
}
