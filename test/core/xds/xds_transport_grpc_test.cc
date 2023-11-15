// Copyright 2021 gRPC authors.
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

#include "src/core/ext/xds/xds_transport_grpc.h"

#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include <gmock/gmock.h>

#include "absl/base/thread_annotations.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gtest/gtest.h"

#include <grpc/support/log.h>
#include <grpcpp/impl/call_op_set.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>
#include <grpcpp/support/interceptor.h>
#include <grpcpp/support/status.h>
#include <grpcpp/support/sync_stream.h>

#include "src/core/ext/xds/xds_bootstrap_grpc.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/json/json_object_loader.h"
#include "src/core/lib/json/json_reader.h"
#include "src/proto/grpc/testing/xds/v3/ads.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/discovery.pb.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"
#include "test/cpp/end2end/xds/xds_utils.h"

namespace grpc_core {
namespace testing {
namespace {

std::string CreateAdsRequest(absl::string_view type_url,
                             const std::vector<std::string>& resource_names) {
  envoy::service::discovery::v3::DiscoveryRequest req;
  // Set type_url.
  std::string type_url_str = absl::StrCat("type.googleapis.com/", type_url);
  req.set_type_url(type_url_str);
  // Add resource_names.
  for (const std::string& resource_name : resource_names) {
    *req.add_resource_names() = resource_name;
  }
  return req.SerializeAsString();
}

using EventHandlerEvent = absl::variant<bool, absl::Status, std::string>;

class AdsService : public envoy::service::discovery::v3::
                       AggregatedDiscoveryService::Service {
 public:
  grpc::Status StreamAggregatedResources(
      grpc::ServerContext* /* context */,
      grpc::ServerReaderWriter<envoy::service::discovery::v3::DiscoveryResponse,
                               envoy::service::discovery::v3::DiscoveryRequest>*
          stream) override {
    gpr_log(GPR_DEBUG, "In here!");
    envoy::service::discovery::v3::DiscoveryRequest req;
    stream->Read(&req);
    gpr_log(GPR_DEBUG, "%s", req.DebugString().c_str());
    envoy::service::discovery::v3::DiscoveryResponse res;
    stream->WriteLast(res, grpc::WriteOptions());
    return grpc::Status(grpc::StatusCode::UNIMPLEMENTED, "Not implemented yet");
  }
};

class TestEventHandler
    : public XdsTransportFactory::XdsTransport::StreamingCall::EventHandler {
 public:
  explicit TestEventHandler(std::vector<EventHandlerEvent>* events)
      : events_(events) {}

  void OnRequestSent(bool ok) override {
    gpr_log(GPR_DEBUG, "on request sent %d", ok);
    events_->emplace_back(ok);
  }

  void OnRecvMessage(absl::string_view payload) override {
    gpr_log(GPR_DEBUG, "message recv");
    events_->emplace_back(std::string(payload));
  }

  void OnStatusReceived(absl::Status status) override {
    gpr_log(GPR_DEBUG, "status %s", std::string(status.message()).c_str());
    events_->emplace_back(std::move(status));
  }

 private:
  std::vector<EventHandlerEvent>* events_;
};

class AdsServer {
 public:
  AdsServer() : server_thread_(ServerThread, this) {
    MutexLock lock(&mu_);
    if (url_.empty()) {
      cond_.Wait(&mu_);
    }
  }

  ~AdsServer() {
    server_->Shutdown();
    server_thread_.join();
  }

  std::string url() {
    MutexLock lock(&mu_);
    return url_;
  }

 private:
  static void ServerThread(AdsServer* ads_server) { ads_server->Run(); }

  void Run() {
    {
      MutexLock lock(&mu_);
      url_ = absl::StrFormat("localhost:%d", grpc_pick_unused_port_or_die());
      grpc::ServerBuilder builder;
      builder.AddListeningPort(url_, grpc::InsecureServerCredentials());
      builder.RegisterService(&ads_service_);
      server_ = builder.BuildAndStart();
      cond_.SignalAll();
    }
    server_->Wait();
  }

  std::thread server_thread_;
  Mutex mu_;
  CondVar cond_;
  std::string url_ ABSL_GUARDED_BY(mu_);
  AdsService ads_service_ ABSL_GUARDED_BY(mu_);
  std::unique_ptr<grpc::Server> server_;
};

TEST(GrpcTransportTest, DISABLED_WaitsWithAdsRead) {
  AdsServer ads_server;
  ExecCtx exec_ctx;
  ChannelArgs args;
  auto factory = MakeOrphanable<GrpcXdsTransportFactory>(args);
  std::string bootstrap = grpc::testing::XdsBootstrapBuilder()
                              .SetDefaultServer(ads_server.url())
                              .SetXdsChannelCredentials("insecure")
                              .Build();
  gpr_log(GPR_ERROR, "%s", bootstrap.c_str());
  auto json = JsonParse(
      absl::StrFormat("{\"server_uri\": \"%s\", "
                      "\"channel_creds\": [{ \"type\": \"insecure\" }]}",
                      ads_server.url()));
  ASSERT_TRUE(json.ok()) << json.status();
  absl::StatusOr<GrpcXdsBootstrap::GrpcXdsServer> server =
      LoadFromJson<GrpcXdsBootstrap::GrpcXdsServer>(*json);
  ASSERT_TRUE(server.ok()) << server.status();
  absl::Status status;
  std::vector<absl::Status> statuses;
  auto transport = factory->Create(
      *server, [&statuses](auto s) { statuses.emplace_back(std::move(s)); },
      &status);
  ASSERT_TRUE(status.ok()) << status;
  std::vector<EventHandlerEvent> events;
  auto call = transport->CreateStreamingCall(
      "boop", std::make_unique<TestEventHandler>(&events));
  call->SendMessage(CreateAdsRequest("aaa", {"r1", "r2"}));
  auto deadline = absl::Now() + absl::Seconds(15);
  while (events.empty() && deadline > absl::Now() && statuses.empty() &&
         status.ok()) {
    gpr_log(GPR_DEBUG, "Waiting!");
    absl::SleepFor(absl::Seconds(3));
  }
  EXPECT_THAT(events, ::testing::IsEmpty());
  EXPECT_THAT(statuses, ::testing::IsEmpty());
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  const auto result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
