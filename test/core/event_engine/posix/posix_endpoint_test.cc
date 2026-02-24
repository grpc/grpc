// Copyright 2022 gRPC Authors
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

#include "src/core/lib/event_engine/posix_engine/posix_endpoint.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/grpc.h>
#include <grpc/impl/channel_arg_names.h>

#include <algorithm>
#include <chrono>
#include <list>
#include <memory>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

#include "src/core/config/config_vars.h"
#include "src/core/handshaker/security/secure_endpoint.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/event_engine/channel_args_endpoint_config.h"
#include "src/core/lib/event_engine/poller.h"
#include "src/core/lib/event_engine/posix_engine/event_poller.h"
#include "src/core/lib/event_engine/posix_engine/event_poller_posix_default.h"
#include "src/core/lib/event_engine/posix_engine/posix_engine.h"
#include "src/core/lib/event_engine/posix_engine/posix_engine_closure.h"
#include "src/core/lib/event_engine/posix_engine/tcp_socket_utils.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/iomgr/event_engine_shims/endpoint.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/tsi/fake_transport_security.h"
#include "src/core/tsi/transport_security_grpc.h"
#include "src/core/util/dual_ref_counted.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/notification.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/wait_for_single_owner.h"
#include "test/core/event_engine/event_engine_test_utils.h"
#include "test/core/event_engine/posix/posix_engine_test_utils.h"
#include "test/core/event_engine/test_suite/posix/oracle_event_engine_posix.h"
#include "test/core/test_util/port.h"
#include "gtest/gtest.h"
#include "absl/log/log.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"

namespace grpc_event_engine {
namespace experimental {

namespace {

using Endpoint = ::grpc_event_engine::experimental::EventEngine::Endpoint;
using Listener = ::grpc_event_engine::experimental::EventEngine::Listener;
using namespace std::chrono_literals;

constexpr int kMinMessageSize = 1024;
constexpr int kNumConnections = 10;
constexpr int kNumExchangedMessages = 100;
std::atomic<int> g_num_active_connections{0};

struct Connection {
  std::unique_ptr<EventEngine::Endpoint> client_endpoint;
  std::unique_ptr<EventEngine::Endpoint> server_endpoint;
};

std::list<Connection> CreateConnectedEndpoints(
    PosixEventPoller& poller, bool is_zero_copy_enabled, int num_connections,
    std::shared_ptr<EventEngine> posix_ee,
    std::shared_ptr<EventEngine> oracle_ee) {
  std::list<Connection> connections;
  auto memory_quota = std::make_unique<grpc_core::MemoryQuota>(
      grpc_core::MakeRefCounted<grpc_core::channelz::ResourceQuotaNode>("bar"));
  std::string target_addr = absl::StrCat(
      "ipv6:[::1]:", std::to_string(grpc_pick_unused_port_or_die()));
  auto resolved_addr = URIToResolvedAddress(target_addr);
  GRPC_CHECK_OK(resolved_addr);
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
  auto quota = grpc_core::ResourceQuota::Default();
  args = args.Set(GRPC_ARG_RESOURCE_QUOTA, quota);
  if (is_zero_copy_enabled) {
    args = args.Set(GRPC_ARG_TCP_TX_ZEROCOPY_ENABLED, 1);
    args = args.Set(GRPC_ARG_TCP_TX_ZEROCOPY_SEND_BYTES_THRESHOLD,
                    kMinMessageSize);
  }
  ChannelArgsEndpointConfig config(args);
  auto listener = oracle_ee->CreateListener(
      std::move(accept_cb),
      [](absl::Status status) { ASSERT_TRUE(status.ok()); }, config,
      std::make_unique<grpc_core::MemoryQuota>(
          grpc_core::MakeRefCounted<grpc_core::channelz::ResourceQuotaNode>(
              "bar")));
  GRPC_CHECK_OK(listener);

  EXPECT_TRUE((*listener)->Bind(*resolved_addr).ok());
  EXPECT_TRUE((*listener)->Start().ok());

  // Create client socket and connect to the target address.
  for (int i = 0; i < num_connections; ++i) {
    int client_fd = ConnectToServerOrDie(*resolved_addr);
    EventHandle* handle =
        poller.CreateHandle(poller.posix_interface().Adopt(client_fd), "test",
                            poller.CanTrackErrors());
    EXPECT_NE(handle, nullptr);
    server_signal->WaitForNotification();
    EXPECT_NE(server_endpoint, nullptr);
    ++g_num_active_connections;
    PosixTcpOptions options = TcpOptionsFromEndpointConfig(config);
    connections.push_back(Connection{
        CreatePosixEndpoint(
            handle,
            PosixEngineClosure::TestOnlyToClosure(
                [&poller](absl::Status /*status*/) {
                  if (--g_num_active_connections == 0) {
                    poller.Kick();
                  }
                }),
            posix_ee,
            options.resource_quota->memory_quota()->CreateMemoryAllocator(
                "test"),
            options),
        std::move(server_endpoint)});
    delete server_signal;
    server_signal = new grpc_core::Notification();
  }
  delete server_signal;
  return connections;
}

}  // namespace

std::string TestScenarioName(const ::testing::TestParamInfo<bool>& info) {
  return absl::StrCat("is_zero_copy_enabled_", info.param);
}

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
  void Orphaned() override { signal.Notify(); }
  void Start() {
    // Start executing Work(..).
    engine_->Run([this]() { Work(); });
  }

  void Wait() {
    signal.WaitForNotification();
    WeakUnref();
  }

 private:
  void Work() {
    auto result = poller_->Work(24h, [this]() {
      // Schedule next work instantiation immediately and take a Ref for
      // the next instantiation.
      Ref().release();
      engine_->Run([this]() { Work(); });
    });
    ASSERT_TRUE(result == Poller::WorkResult::kOk ||
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
  grpc_core::Notification signal;
};

class PosixEndpointTestBase {
 public:
  void SetUp() {
    oracle_ee_ = std::make_shared<PosixOracleEventEngine>();
    thread_pool_ =
        std::make_shared<grpc_event_engine::experimental::TestThreadPool>(
            posix_ee_.get());
    EXPECT_NE(thread_pool_, nullptr);
    poller_ = MakeDefaultPoller(thread_pool_);
    posix_ee_ = PosixEventEngine::MakeTestOnlyPosixEventEngine(poller_);
    EXPECT_NE(posix_ee_, nullptr);
    thread_pool_->ChangeCurrentEventEngine(posix_ee_.get());
    if (poller_ != nullptr) {
      LOG(INFO) << "Using poller: " << poller_->Name();
    }
  }

  void TearDown() {
    grpc_core::WaitForSingleOwner(std::move(posix_ee_));
    grpc_core::WaitForSingleOwner(std::move(oracle_ee_));
  }

  TestThreadPool* thread_pool() const { return thread_pool_.get(); }

  std::shared_ptr<EventEngine> GetPosixEE() { return posix_ee_; }

  std::shared_ptr<EventEngine> GetOracleEE() { return oracle_ee_; }

  PosixEventPoller* PosixPoller() { return poller_.get(); }

 private:
  std::shared_ptr<PosixEventPoller> poller_;
  std::shared_ptr<TestThreadPool> thread_pool_;
  std::shared_ptr<EventEngine> posix_ee_;
  std::shared_ptr<EventEngine> oracle_ee_;
};

class PosixEndpointTest : public PosixEndpointTestBase,
                          public ::testing::TestWithParam<bool> {
  void SetUp() override { PosixEndpointTestBase::SetUp(); }
  void TearDown() override { PosixEndpointTestBase::TearDown(); }
};

TEST_P(PosixEndpointTest, ConnectExchangeBidiDataTransferTest) {
  if (PosixPoller() == nullptr) {
    return;
  }
  Worker* worker = new Worker(GetPosixEE(), PosixPoller());
  worker->Start();
  {
    auto connections = CreateConnectedEndpoints(*PosixPoller(), GetParam(), 1,
                                                GetPosixEE(), GetOracleEE());
    auto it = connections.begin();
    auto client_endpoint = std::move((*it).client_endpoint);
    auto server_endpoint = std::move((*it).server_endpoint);
    EXPECT_NE(client_endpoint, nullptr);
    EXPECT_NE(server_endpoint, nullptr);
    connections.erase(it);

    // Alternate message exchanges between client -- server and server --
    // client.
    for (int i = 0; i < kNumExchangedMessages; i++) {
      // Send from client to server and verify data read at the server.
      ASSERT_TRUE(SendValidatePayload(GetNextSendMessage(),
                                      client_endpoint.get(),
                                      server_endpoint.get())
                      .ok());
      // Send from server to client and verify data read at the client.
      ASSERT_TRUE(SendValidatePayload(GetNextSendMessage(),
                                      server_endpoint.get(),
                                      client_endpoint.get())
                      .ok());
    }
  }
  worker->Wait();
}

// Create  N connections and exchange and verify random number of messages over
// each connection in parallel.
TEST_P(PosixEndpointTest, MultipleIPv6ConnectionsToOneOracleListenerTest) {
  if (PosixPoller() == nullptr) {
    return;
  }
  Worker* worker = new Worker(GetPosixEE(), PosixPoller());
  worker->Start();
  auto connections = CreateConnectedEndpoints(
      *PosixPoller(), GetParam(), kNumConnections, GetPosixEE(), GetOracleEE());
  std::vector<std::thread> threads;
  // Create one thread for each connection. For each connection, create
  // 2 more worker threads: to exchange and verify bi-directional data transfer.
  threads.reserve(kNumConnections);
  for (int i = 0; i < kNumConnections; i++) {
    // For each connection, simulate a parallel bi-directional data transfer.
    // All bi-directional transfers are run in parallel across all connections.
    auto it = connections.begin();
    auto client_endpoint = std::move((*it).client_endpoint);
    auto server_endpoint = std::move((*it).server_endpoint);
    EXPECT_NE(client_endpoint, nullptr);
    EXPECT_NE(server_endpoint, nullptr);
    connections.erase(it);
    threads.emplace_back([client_endpoint = std::move(client_endpoint),
                          server_endpoint = std::move(server_endpoint)]() {
      std::vector<std::thread> workers;
      workers.reserve(2);
      auto worker = [client_endpoint = client_endpoint.get(),
                     server_endpoint =
                         server_endpoint.get()](bool client_to_server) {
        for (int i = 0; i < kNumExchangedMessages; i++) {
          // If client_to_server is true, send from client to server and
          // verify data read at the server. Otherwise send data from server
          // to client and verify data read at client.
          if (client_to_server) {
            EXPECT_TRUE(SendValidatePayload(GetNextSendMessage(),
                                            client_endpoint, server_endpoint)
                            .ok());
          } else {
            EXPECT_TRUE(SendValidatePayload(GetNextSendMessage(),
                                            server_endpoint, client_endpoint)
                            .ok());
          }
        }
      };
      // worker[0] simulates a flow from client to server endpoint
      workers.emplace_back([&worker]() { worker(true); });
      // worker[1] simulates a flow from server to client endpoint
      workers.emplace_back([&worker]() { worker(false); });
      workers[0].join();
      workers[1].join();
    });
  }
  for (auto& t : threads) {
    t.join();
  }
  worker->Wait();
}

// Test with zero copy enabled and disabled.
INSTANTIATE_TEST_SUITE_P(PosixEndpoint, PosixEndpointTest,
                         ::testing::ValuesIn({false, true}), &TestScenarioName);

struct PosixSecureEndpointTestParams {
  bool has_leftover_bytes;
  bool use_zero_copy_protector;
};

class PosixSecureEndpointTest
    : public PosixEndpointTestBase,
      public ::testing::TestWithParam<PosixSecureEndpointTestParams> {
 public:
  void SetUp() override {
    PosixEndpointTestBase::SetUp();
    if (GetParam().has_leftover_bytes) {
      grpc_slice leftover_slice =
          grpc_slice_from_copied_string(leftover_data_str_.c_str());
      leftover_data_.Append(
          grpc_core::Slice(grpc_core::CSliceRef(leftover_slice)));
    }
    client_zero_copy_protector_ =
        tsi_create_fake_zero_copy_grpc_protector(nullptr);
    server_zero_copy_protector_ =
        tsi_create_fake_zero_copy_grpc_protector(nullptr);
    client_protector_ = tsi_create_fake_frame_protector(nullptr);
    server_protector_ = tsi_create_fake_frame_protector(nullptr);
  }

  void TearDown() override {
    if (GetParam().use_zero_copy_protector) {
      tsi_frame_protector_destroy(client_protector_);
      tsi_frame_protector_destroy(server_protector_);
    } else {
      tsi_zero_copy_grpc_protector_destroy(client_zero_copy_protector_);
      tsi_zero_copy_grpc_protector_destroy(server_zero_copy_protector_);
    }
    grpc_slice_unref(encrypted_leftover_data_);
    leftover_data_.Clear();
    PosixEndpointTestBase::TearDown();
  }

  grpc_core::OrphanablePtr<grpc_endpoint> CreateSecureEndpoint(
      std::unique_ptr<EventEngine::Endpoint> endpoint, bool leftover,
      bool use_zero_copy_protector, bool is_client) {
    if (leftover) {
      EncryptLeftoverBytes(&leftover_data_, server_protector_,
                           &encrypted_leftover_data_);
    }
    grpc_core::ChannelArgs args;
    auto quota = grpc_core::ResourceQuota::Default();
    args = args.Set(GRPC_ARG_RESOURCE_QUOTA, quota);
    return grpc_secure_endpoint_create(
        use_zero_copy_protector
            ? nullptr
            : (is_client ? client_protector_ : server_protector_),
        use_zero_copy_protector ? (is_client ? client_zero_copy_protector_
                                             : server_zero_copy_protector_)
                                : nullptr,
        grpc_core::OrphanablePtr<grpc_endpoint>(
            grpc_event_engine_endpoint_create(std::move(endpoint))),
        (leftover) ? &encrypted_leftover_data_ : nullptr, (leftover) ? 1 : 0,
        args);
  }

  std::string GetLeftoverDataStr() { return leftover_data_str_; }

 private:
  void EncryptLeftoverBytes(grpc_core::SliceBuffer* leftover_data,
                            tsi_frame_protector* protector,
                            grpc_slice* encrypted_leftover_data) {
    tsi_result result;
    size_t still_pending_size;
    size_t total_buffer_size = 8192;
    size_t buffer_size = total_buffer_size;
    uint8_t* encrypted_buffer = static_cast<uint8_t*>(gpr_malloc(buffer_size));
    uint8_t* cur = encrypted_buffer;
    for (unsigned i = 0; i < leftover_data->Count(); i++) {
      grpc_slice unencrypted = leftover_data->c_slice_at(i);
      uint8_t* message_bytes = GRPC_SLICE_START_PTR(unencrypted);
      size_t message_size = GRPC_SLICE_LENGTH(unencrypted);
      while (message_size > 0) {
        size_t protected_buffer_size_to_send = buffer_size;
        size_t processed_message_size = message_size;
        result = tsi_frame_protector_protect(protector, message_bytes,
                                             &processed_message_size, cur,
                                             &protected_buffer_size_to_send);
        EXPECT_EQ(result, TSI_OK);
        message_bytes += processed_message_size;
        message_size -= processed_message_size;
        cur += protected_buffer_size_to_send;
        EXPECT_GE(buffer_size, protected_buffer_size_to_send);
        buffer_size -= protected_buffer_size_to_send;
      }
      grpc_slice_unref(unencrypted);
    }
    do {
      size_t protected_buffer_size_to_send = buffer_size;
      result = tsi_frame_protector_protect_flush(
          protector, cur, &protected_buffer_size_to_send, &still_pending_size);
      EXPECT_EQ(result, TSI_OK);
      cur += protected_buffer_size_to_send;
      EXPECT_GE(buffer_size, protected_buffer_size_to_send);
      buffer_size -= protected_buffer_size_to_send;
    } while (still_pending_size > 0);
    *encrypted_leftover_data = grpc_slice_from_copied_buffer(
        reinterpret_cast<const char*>(encrypted_buffer),
        total_buffer_size - buffer_size);
    gpr_free(encrypted_buffer);
  }

  tsi_frame_protector* client_protector_ = nullptr;
  tsi_zero_copy_grpc_protector* client_zero_copy_protector_ = nullptr;
  tsi_frame_protector* server_protector_ = nullptr;
  tsi_zero_copy_grpc_protector* server_zero_copy_protector_ = nullptr;
  grpc_core::SliceBuffer leftover_data_;
  grpc_slice encrypted_leftover_data_ = grpc_empty_slice();
  std::string leftover_data_str_ = "hello world 12345678900987654321";
};

TEST_P(PosixSecureEndpointTest, ConnectExchangeBidiDataTransferTest) {
  if (PosixPoller() == nullptr) {
    return;
  }
  Worker* worker = new Worker(GetPosixEE(), PosixPoller());
  worker->Start();
  {
    auto connections = CreateConnectedEndpoints(*PosixPoller(), true, 1,
                                                GetPosixEE(), GetOracleEE());
    auto it = connections.begin();
    auto client_endpoint = std::move((*it).client_endpoint);
    auto server_endpoint = std::move((*it).server_endpoint);
    EXPECT_NE(client_endpoint, nullptr);
    EXPECT_NE(server_endpoint, nullptr);
    auto client_secure_endpoint = CreateSecureEndpoint(
        std::move(client_endpoint),
        /*leftover=*/GetParam().has_leftover_bytes,
        /*use_zero_copy_protector=*/GetParam().use_zero_copy_protector,
        /*is_client=*/true);
    auto server_secure_endpoint = CreateSecureEndpoint(
        std::move(server_endpoint), /*leftover=*/false,
        /*use_zero_copy_protector=*/GetParam().use_zero_copy_protector,
        /*is_client=*/false);
    connections.erase(it);

    if (GetParam().has_leftover_bytes) {
      grpc_core::Notification read_done;
      SliceBuffer read_buffer;
      if (grpc_get_wrapped_event_engine_endpoint(client_secure_endpoint.get())
              ->Read(
                  [&](absl::Status status) {
                    CHECK_OK(status) << "Failed to read leftover data from "
                                        "client secure endpoint";
                    read_done.Notify();
                  },
                  &read_buffer, {})) {
        read_done.Notify();
      }
      read_done.WaitForNotification();
      EXPECT_EQ(read_buffer.Count(), 1);
      EXPECT_EQ(read_buffer.TakeFirst().as_string_view(), GetLeftoverDataStr());
    }

    // Alternate message exchanges between client -- server and server --
    // client. If we are not using the zero copy protector, we need to make sure
    // to include the secure frame header size in the number of bytes we expect
    // to read from the endpoint. For the fake frame protector, the header is 4
    // bytes.
    std::string client_msg;
    std::string server_msg;
    for (int i = 0; i < kNumExchangedMessages; i++) {
      // Send from client to server and verify data read at the server.
      client_msg = GetNextSendMessage();
      ASSERT_TRUE(
          SendValidatePayload(
              client_msg,
              grpc_get_wrapped_event_engine_endpoint(
                  client_secure_endpoint.get()),
              grpc_get_wrapped_event_engine_endpoint(
                  server_secure_endpoint.get()),
              /*read_hint_bytes=*/
              (GetParam().use_zero_copy_protector ? -1 : client_msg.size() + 4))
              .ok());
      // Send from server to client and verify data read at the client.
      server_msg = GetNextSendMessage();
      ASSERT_TRUE(
          SendValidatePayload(
              server_msg,
              grpc_get_wrapped_event_engine_endpoint(
                  server_secure_endpoint.get()),
              grpc_get_wrapped_event_engine_endpoint(
                  client_secure_endpoint.get()),
              /*read_hint_bytes=*/
              (GetParam().use_zero_copy_protector ? -1 : server_msg.size() + 4))
              .ok());
    }
  }
  worker->Wait();
}

std::string SecureEndpointTestScenarioName(
    const ::testing::TestParamInfo<PosixSecureEndpointTestParams>& info) {
  return absl::StrCat("_has_leftover_bytes_", info.param.has_leftover_bytes,
                      "_use_zero_copy_protector_",
                      info.param.use_zero_copy_protector);
}

// Test with zero copy enabled and disabled, and using secure endpoints.
INSTANTIATE_TEST_SUITE_P(
    PosixSecureEndpoint, PosixSecureEndpointTest,
    ::testing::ValuesIn(
        {PosixSecureEndpointTestParams{.has_leftover_bytes = false,
                                       .use_zero_copy_protector = false},
         PosixSecureEndpointTestParams{.has_leftover_bytes = false,
                                       .use_zero_copy_protector = true},
         PosixSecureEndpointTestParams{.has_leftover_bytes = true,
                                       .use_zero_copy_protector = false},
         PosixSecureEndpointTestParams{.has_leftover_bytes = true,
                                       .use_zero_copy_protector = true}}),
    &SecureEndpointTestScenarioName);

}  // namespace experimental
}  // namespace grpc_event_engine

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  auto poll_strategy = grpc_core::ConfigVars::Get().PollStrategy();
  auto strings = absl::StrSplit(poll_strategy, ',');
  if (std::find(strings.begin(), strings.end(), "none") != strings.end()) {
    // Skip the test entirely if poll strategy is none.
    return 0;
  }
  // TODO(ctiller): EventEngine temporarily needs grpc to be initialized first
  // until we clear out the iomgr shutdown code.
  grpc_init();
  int r = RUN_ALL_TESTS();
  grpc_shutdown();
  return r;
}
