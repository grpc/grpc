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

#include <grpc/event_engine/event_engine.h>
#include <grpc/grpc.h>
#include <grpc/status.h>
#include <grpcpp/server.h>

#include <memory>
#include <string>
#include <utility>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/ext/transport/chaotic_good/client/chaotic_good_connector.h"
#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/server/server.h"
#include "src/core/util/notification.h"
#include "src/core/util/time.h"
#include "src/core/util/uri.h"
#include "test/core/event_engine/event_engine_test_utils.h"
#include "test/core/test_util/build.h"
#include "test/core/test_util/port.h"
#include "test/core/test_util/test_config.h"

namespace grpc_core {
namespace chaotic_good {
namespace testing {
class ChaoticGoodServerTest : public ::testing::Test {
 public:
  ChaoticGoodServerTest() {
    StartServer();
    ConstructConnector();
  }

  ~ChaoticGoodServerTest() override {
    {
      ExecCtx exec_ctx;
      if (connecting_successful_) {
        connecting_result_.transport->Orphan();
      }
      if (connector_ != nullptr) connector_->Shutdown(absl::CancelledError());
      connector_.reset();
    }
    args_.channel_args = ChannelArgs();
    auto* shutdown_cq = grpc_completion_queue_create_for_pluck(nullptr);
    grpc_server_shutdown_and_notify(server_, shutdown_cq, nullptr);
    auto ev = grpc_completion_queue_pluck(
        shutdown_cq, nullptr, grpc_timeout_milliseconds_to_deadline(15000),
        nullptr);
    if (ev.type == GRPC_QUEUE_TIMEOUT) {
      AsanAssertNoLeaks();
    }
    CHECK_EQ(ev.type, GRPC_OP_COMPLETE);
    CHECK_EQ(ev.tag, nullptr);
    grpc_completion_queue_destroy(shutdown_cq);
    grpc_server_destroy(server_);
  }

  void StartServer() {
    port_ = grpc_pick_unused_port_or_die();
    addr_ = absl::StrCat("[::1]:", port_);
    server_ = grpc_server_create(nullptr, nullptr);
    grpc_server_add_chaotic_good_port(server_, addr_.c_str());
    grpc_server_start(server_);
  }

  void ConstructConnector() {
    auto uri = URI::Parse("ipv6:" + addr_);
    CHECK_OK(uri);
    CHECK(grpc_parse_uri(*uri, &resolved_addr_));
    args_.address = &resolved_addr_;
    args_.deadline = Timestamp::Now() + Duration::Seconds(5);
    args_.channel_args = channel_args();
    connector_ = MakeRefCounted<ChaoticGoodConnector>();
  }

 protected:
  static void OnConnectingFinished(void* arg, grpc_error_handle error) {
    LOG(ERROR) << "OnConnectingFinished: " << arg << " " << error.ToString();
    ChaoticGoodServerTest* test = static_cast<ChaoticGoodServerTest*>(arg);
    test->connecting_successful_ = error.ok();
    test->connect_finished_.Notify();
  }

  ChannelArgs channel_args() {
    return CoreConfiguration::Get()
        .channel_args_preconditioning()
        .PreconditionChannelArgs(nullptr);
  }

  grpc_server* server_;
  Server* core_server_;
  ChaoticGoodConnector::Args args_;
  ChaoticGoodConnector::Result connecting_result_;
  bool connecting_successful_ = false;
  grpc_closure on_connecting_finished_;
  Notification connect_finished_;
  int port_;
  std::string addr_;
  grpc_resolved_address resolved_addr_;
  RefCountedPtr<ChaoticGoodConnector> connector_;
};

TEST_F(ChaoticGoodServerTest, Connect) {
  if (!IsChaoticGoodFramingLayerEnabled()) {
    GTEST_SKIP() << "Chaotic Good framing layer is not enabled";
  }
  GRPC_CLOSURE_INIT(&on_connecting_finished_, OnConnectingFinished, this,
                    grpc_schedule_on_exec_ctx);
  connector_->Connect(args_, &connecting_result_, &on_connecting_finished_);
  connect_finished_.WaitForNotification();
}

TEST_F(ChaoticGoodServerTest, ConnectAndShutdown) {
  if (!IsChaoticGoodFramingLayerEnabled()) {
    GTEST_SKIP() << "Chaotic Good framing layer is not enabled";
  }
  Notification connect_finished;
  GRPC_CLOSURE_INIT(&on_connecting_finished_, OnConnectingFinished, this,
                    grpc_schedule_on_exec_ctx);
  {
    ExecCtx exec_ctx;
    connector_->Connect(args_, &connecting_result_, &on_connecting_finished_);
    connector_->Shutdown(absl::InternalError("shutdown"));
  }
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
