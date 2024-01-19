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

#include <memory>
#include <string>
#include <utility>

#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/grpc.h>
#include <grpc/status.h>
#include <grpcpp/server.h>

#include "src/core/ext/transport/chaotic_good/client/chaotic_good_connector.h"
#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/notification.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/surface/server.h"
#include "src/core/lib/uri/uri_parser.h"
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
    auto resource_quota = ResourceQuota::Default();
    channel_args_ =
        channel_args_.Set(GRPC_ARG_RESOURCE_QUOTA, std::move(resource_quota));
    StartServer();
    ConstructConnector();
  }
  ~ChaoticGoodServerTest() override {
    core_server_->StopListening();
    auto* shutdown_cq = grpc_completion_queue_create_for_pluck(nullptr);
    core_server_->ShutdownAndNotify(shutdown_cq, nullptr);
    grpc_completion_queue_destroy(shutdown_cq);
  }

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
  static void OnConnectingFinished(void* arg, grpc_error_handle) {
    Notification* connect_finished_ = static_cast<Notification*>(arg);
    connect_finished_->Notify();
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
};

TEST_F(ChaoticGoodServerTest, Connect) {
  Notification connect_finished;
  GRPC_CLOSURE_INIT(&on_connecting_finished_, OnConnectingFinished,
                    &connect_finished, grpc_schedule_on_exec_ctx);
  connector_->Connect(args_, &connecting_result_, &on_connecting_finished_);
  connect_finished.WaitForNotification();
}

TEST_F(ChaoticGoodServerTest, ConnectAndShutdown) {
  Notification connect_finished;
  GRPC_CLOSURE_INIT(&on_connecting_finished_, OnConnectingFinished,
                    &connect_finished, grpc_schedule_on_exec_ctx);
  connector_->Connect(args_, &connecting_result_, &on_connecting_finished_);
  connector_->Shutdown(absl::InternalError("shutdown"));
  connect_finished.WaitForNotification();
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
