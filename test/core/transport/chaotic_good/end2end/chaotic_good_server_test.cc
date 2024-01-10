// Copyright 2024 gRPC authors.
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

#include "src/core/ext/transport/chaotic_good/server/chaotic_good_server.h"

#include <cstdint>
#include <cstdio>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

#include "absl/random/bit_gen_ref.h"
#include "absl/random/random.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/memory_allocator.h>
#include <grpc/event_engine/slice.h>  // IWYU pragma: keep
#include <grpc/event_engine/slice_buffer.h>
#include <grpc/grpc.h>
#include <grpc/status.h>
#include <grpcpp/server.h>

#include "src/core/ext/transport/chaotic_good/client/chaotic_good_connector.h"
#include "src/core/ext/transport/chaotic_good/frame.h"
#include "src/core/ext/transport/chaotic_good/frame_header.h"
#include "src/core/ext/transport/chttp2/transport/hpack_encoder.h"
#include "src/core/ext/transport/chttp2/transport/hpack_parser.h"
#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/event_engine/channel_args_endpoint_config.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/gprpp/notification.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/event_engine_wakeup_scheduler.h"
#include "src/core/lib/promise/join.h"
#include "src/core/lib/promise/latch.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/promise/race.h"
#include "src/core/lib/promise/seq.h"
#include "src/core/lib/promise/sleep.h"
#include "src/core/lib/promise/try_join.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/lib/promise/wait_for_callback.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/surface/server.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/promise_endpoint.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/lib/uri/uri_parser.h"
#include "src/cpp/server/secure_server_credentials.h"
#include "test/core/util/port.h"

namespace grpc_core {
namespace chaotic_good {
namespace testing {
using grpc_event_engine::experimental::EventEngine;
class ChaoticGoodServerTest : public ::testing::Test {
 public:
  ChaoticGoodServerTest() {
    event_engine_ = std::shared_ptr<EventEngine>(
        grpc_event_engine::experimental::CreateEventEngine());
    channel_args_ = channel_args_.SetObject(event_engine_);
    resource_quota_ = ResourceQuota::Default();
    channel_args_ = channel_args_.Set(GRPC_ARG_RESOURCE_QUOTA, resource_quota_);
    StartServer();
    ConstructConnector();
  }
  ~ChaoticGoodServerTest() { core_server_->StopListening(); }

  void StartServer() {
    port_ = grpc_pick_unused_port_or_die();
    addr_ = absl::StrCat("ipv6:[::1]:", port_);
    ExecCtx exec_ctx;
    server_ = grpc_server_create(nullptr, nullptr);
    core_server_ = Server::FromC(server_);
    listener_ = std::make_shared<ChaoticGoodServerListener>(core_server_,
                                                            channel_args_);
    auto port = listener_->Bind(addr_.c_str());
    EXPECT_TRUE(port.ok());
    EXPECT_EQ(port.value(), port_);
    auto status = listener_->StartListening();
    EXPECT_TRUE(status.ok());
  }

  void ConstructConnector() {
    auto uri = URI::Parse(addr_);
    GPR_ASSERT(uri.ok());
    GPR_ASSERT(grpc_parse_uri(*uri, &resolved_addr_));
    args_.address = &resolved_addr_;
    args_.deadline = Timestamp::Now() + Duration::Seconds(5);
    args_.channel_args = channel_args_;
    connector_ = std::make_shared<ChaoticGoodConnector>();
  }

 protected:
  static void OnConnectingFinished(void* arg, grpc_error_handle error) {
    ChaoticGoodServerTest* self = static_cast<ChaoticGoodServerTest*>(arg);
    if (error.ok()) {
      // connect succeeded.
      // Initialize the client transport.
    } else {
      // connect failed.
    }
    self->connect_finished_.Notify();
  }
  grpc_server* server_;
  Server* core_server_;
  ChannelArgs channel_args_;
  ChaoticGoodConnector::Args args_;
  ChaoticGoodConnector::Result connecting_result_;
  grpc_closure on_connecting_finished_;
  int port_;
  std::string addr_;
  grpc_resolved_address resolved_addr_;
  std::shared_ptr<ChaoticGoodServerListener> listener_;
  std::shared_ptr<ChaoticGoodConnector> connector_;
  std::shared_ptr<EventEngine> event_engine_;
  std::unique_ptr<MemoryQuota> memory_quota_;
  ResourceQuotaRefPtr resource_quota_;
  Notification connect_finished_;
};

TEST_F(ChaoticGoodServerTest, Connect) {
  GRPC_CLOSURE_INIT(&on_connecting_finished_, OnConnectingFinished, this,
                    grpc_schedule_on_exec_ctx);
  connector_->Connect(args_, &connecting_result_, &on_connecting_finished_);
  connect_finished_.WaitForNotification();
}

TEST_F(ChaoticGoodServerTest, ConnectAndShutdown) {
  GRPC_CLOSURE_INIT(&on_connecting_finished_, OnConnectingFinished, this,
                    grpc_schedule_on_exec_ctx);
  connector_->Connect(args_, &connecting_result_, &on_connecting_finished_);
  connector_->Shutdown(absl::InternalError("shutdown"));
  connect_finished_.WaitForNotification();
}
}  // namespace testing
}  // namespace chaotic_good
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  // Must call to create default EventEngine.
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
