//
// Copyright 2022 gRPC authors.
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
//

#include "src/core/ext/xds/xds_client.h"

#include <deque>
#include <map>
#include <memory>
#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/core/ext/xds/xds_bootstrap.h"
#include "src/core/ext/xds/xds_resource_type_impl.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/json/json_object_loader.h"
#include "src/proto/grpc/testing/xds/v3/discovery.grpc.pb.h"
#include "test/core/util/test_config.h"
#include "test/core/xds/xds_transport_fake.h"

using envoy::service::discovery::v3::DiscoveryRequest;
using envoy::service::discovery::v3::DiscoveryResponse;

namespace grpc_core {
namespace testing {
namespace {

class XdsClientTest : public ::testing::Test {
 protected:
  // A fake bootstrap implementation that allows tests to populate the
  // fields however they want.
  class FakeXdsBootstrap : public XdsBootstrap {
   public:
    class Builder {
     public:
      Builder() {
        server_.server_uri = "default_xds_server";
        server_.server_features.insert(
            std::string(XdsServer::kServerFeatureXdsV3));
        node_ = absl::make_unique<Node>();
        node_->id = "xds_client_test";
      }

      Builder& set_server(XdsServer server) {
        server_ = std::move(server);
        return *this;
      }
      Builder& set_node(std::unique_ptr<Node> node) {
        node_ = std::move(node);
        return *this;
      }
      Builder& AddAuthority(std::string name, Authority authority) {
        authorities_[std::move(name)] = std::move(authority);
        return *this;
      }
      std::unique_ptr<XdsBootstrap> Build() {
        auto bootstrap = absl::make_unique<FakeXdsBootstrap>();
        bootstrap->server_ = std::move(server_);
        bootstrap->node_ = std::move(node_);
        bootstrap->authorities_ = std::move(authorities_);
        return bootstrap;
      }

     private:
      XdsServer server_;
      std::unique_ptr<Node> node_;
      std::map<std::string, Authority> authorities_;
    };

    std::string ToString() const override { return "<fake>"; }

    const XdsServer& server() const override { return server_; }
    const Node* node() const override { return node_.get(); }
    const std::map<std::string, Authority>& authorities() const override {
      return authorities_;
    }

   private:
    XdsServer server_;
    std::unique_ptr<Node> node_;
    std::map<std::string, Authority> authorities_;
  };

  // A fake xDS resource type called "test.v3.foo".
  // The payload is a JSON string that parses to an XdsFooResource struct.
  struct XdsFooResource {
    std::string name;
    uint32_t value;

    bool operator==(const XdsFooResource& other) const {
      return name == other.name && value == other.value;
    }

    static const JsonLoaderInterface* JsonLoader(const JsonArgs&) {
      static const auto* loader = JsonObjectLoader<XdsFooResource>()
                                      .Field("name", &XdsFooResource::name)
                                      .Field("value", &XdsFooResource::value)
                                      .Finish();
      return loader;
    }
  };
  class XdsFooResourceType
      : public XdsResourceTypeImpl<XdsFooResourceType, XdsFooResource> {
   public:
    absl::string_view type_url() const override { return "test.v3.foo"; }
    absl::string_view v2_type_url() const override { return "test.v2.foo"; }
    absl::StatusOr<DecodeResult> Decode(
        const XdsResourceType::DecodeContext& context,
        absl::string_view serialized_resource, bool is_v2) const override {
      auto json = Json::Parse(serialized_resource);
      if (!json.ok()) return json.status();
      absl::StatusOr<XdsFooResource> foo = LoadFromJson<XdsFooResource>(*json);
      DecodeResult result;
      if (!foo.ok()) {
        result.resource = foo.status();
      } else {
        result.name = foo->name;
        auto resource = absl::make_unique<ResourceDataSubclass>();
        resource->resource = std::move(*foo);
        result.resource = std::move(resource);
      }
      return std::move(result);
    }
    void InitUpbSymtab(upb_DefPool* symtab) const override {}
  };

  // A watcher implementation that queues delivered watches.
  class FooWatcher : public XdsFooResourceType::WatcherInterface {
   public:
    absl::optional<XdsFooResource> GetNextResource() {
      MutexLock lock(&mu_);
      if (queue_.empty()) return absl::nullopt;
      XdsFooResource foo = std::move(queue_.front());
      queue_.pop_front();
      return foo;
    }

    absl::optional<absl::Status> GetNextError() {
      MutexLock lock(&mu_);
      if (error_queue_.empty()) return absl::nullopt;
      absl::Status status = std::move(error_queue_.front());
      error_queue_.pop_front();
      return status;
    }

   private:
    void OnResourceChanged(XdsFooResource foo) override {
      MutexLock lock(&mu_);
      queue_.push_back(std::move(foo));
    }
    void OnError(absl::Status status) override {
      MutexLock lock(&mu_);
      error_queue_.push_back(std::move(status));
    }
    void OnResourceDoesNotExist() override {
      ASSERT_TRUE(false) << "OnResourceDoesNotExist() called";
    }

    Mutex mu_;
    std::deque<XdsFooResource> queue_ ABSL_GUARDED_BY(&mu_);
    std::deque<absl::Status> error_queue_ ABSL_GUARDED_BY(&mu_);
  };

  void InitXdsClient(FakeXdsBootstrap::Builder bootstrap_builder =
                         FakeXdsBootstrap::Builder()) {
    auto transport_factory = MakeOrphanable<FakeXdsTransportFactory>();
    transport_factory_ = transport_factory->Ref();
    xds_client_ = MakeRefCounted<XdsClient>(bootstrap_builder.Build(),
                                            std::move(transport_factory));
  }

  RefCountedPtr<FooWatcher> StartFooWatch(absl::string_view resource_name) {
    auto watcher = MakeRefCounted<FooWatcher>();
    XdsFooResourceType::StartWatch(xds_client_.get(), resource_name, watcher);
    return watcher;
  }

  void CancelFooWatch(FooWatcher* watcher, absl::string_view resource_name) {
    XdsFooResourceType::CancelWatch(xds_client_.get(), resource_name, watcher);
  }

  absl::optional<DiscoveryRequest> GetRequest(
      FakeXdsTransportFactory::FakeStreamingCall* stream) {
    auto message = stream->GetMessageFromClient();
    EXPECT_TRUE(message.has_value());
    if (!message.has_value()) return absl::nullopt;
    DiscoveryRequest request;
    bool success = request.ParseFromString(*message);
    EXPECT_TRUE(success);
    if (!success) return absl::nullopt;
    return std::move(request);
  }

  void CheckRequestNode(const DiscoveryRequest& request) {
    EXPECT_EQ(request.node().id(), xds_client_->bootstrap().node()->id);
    // FIXME: check other node fields
  }

  RefCountedPtr<FakeXdsTransportFactory> transport_factory_;
  RefCountedPtr<XdsClient> xds_client_;
  ExecCtx exec_ctx_;
};

TEST_F(XdsClientTest, BasicWatch) {
  InitXdsClient();
  // Start a watch for "foo1".
  auto watcher = StartFooWatch("foo1");
  // XdsClient should have created an ADS stream.
  auto stream = transport_factory_->GetStream(
      xds_client_->bootstrap().server(), FakeXdsTransportFactory::kAdsMethod);
  ASSERT_TRUE(stream != nullptr);
  // XdsClient should have sent a request on the ADS stream.
  auto request = GetRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  // Check the request contents.
  CheckRequestNode(*request);
  EXPECT_EQ(request->version_info(), "");
  EXPECT_EQ(request->response_nonce(), "");
  EXPECT_FALSE(request->has_error_detail());
  EXPECT_EQ(request->type_url(),
            absl::StrCat("type.googleapis.com/",
                         XdsFooResourceType::Get()->type_url()));
  EXPECT_THAT(request->resource_names(), ::testing::ElementsAre("foo1"));
  // Send a response.
  DiscoveryResponse response;
  response.set_version_info("1");
  response.set_nonce("A");
  response.set_type_url(absl::StrCat("type.googleapis.com/",
                                     XdsFooResourceType::Get()->type_url()));
  auto* res = response.add_resources();
  res->set_value("{\"name\":\"foo1\",\"value\":6}");
  res->set_type_url(absl::StrCat("type.googleapis.com/",
                                 XdsFooResourceType::Get()->type_url()));
  std::string serialized_response;
  ASSERT_TRUE(response.SerializeToString(&serialized_response));
  stream->SendMessageToClient(serialized_response);
  // XdsClient should have delivered the response to the watcher.
  auto resource = watcher->GetNextResource();
  ASSERT_TRUE(resource.has_value());
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
