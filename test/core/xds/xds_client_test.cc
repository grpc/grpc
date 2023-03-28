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

// TODO(roth): Add the following tests:
// - tests for DumpClientConfigBinary()
// - tests for load-reporting APIs?  (or maybe move those out of XdsClient?)

#include "src/core/ext/xds/xds_client.h"

#include <stdint.h>

#include <algorithm>
#include <deque>
#include <map>
#include <memory>
#include <string>

#include <google/protobuf/any.pb.h>
#include <google/protobuf/struct.pb.h>

#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "absl/types/optional.h"
#include "absl/types/variant.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "upb/def.h"

#include <grpc/grpc.h>
#include <grpc/support/log.h>
#include <grpcpp/impl/codegen/config_protobuf.h>

#include "src/core/ext/xds/xds_bootstrap.h"
#include "src/core/ext/xds/xds_resource_type_impl.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/json/json_args.h"
#include "src/core/lib/json/json_object_loader.h"
#include "src/proto/grpc/testing/xds/v3/base.pb.h"
#include "src/proto/grpc/testing/xds/v3/discovery.pb.h"
#include "test/core/util/scoped_env_var.h"
#include "test/core/util/test_config.h"
#include "test/core/xds/xds_transport_fake.h"

// IWYU pragma: no_include <google/protobuf/message.h>
// IWYU pragma: no_include <google/protobuf/stubs/status.h>
// IWYU pragma: no_include <google/protobuf/unknown_field_set.h>
// IWYU pragma: no_include <google/protobuf/util/json_util.h>

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
    class FakeNode : public Node {
     public:
      const std::string& id() const override { return id_; }
      const std::string& cluster() const override { return cluster_; }
      const std::string& locality_region() const override {
        return locality_region_;
      }
      const std::string& locality_zone() const override {
        return locality_zone_;
      }
      const std::string& locality_sub_zone() const override {
        return locality_sub_zone_;
      }
      const Json::Object& metadata() const override { return metadata_; }

      void set_id(std::string id) { id_ = std::move(id); }
      void set_cluster(std::string cluster) { cluster_ = std::move(cluster); }
      void set_locality_region(std::string locality_region) {
        locality_region_ = std::move(locality_region);
      }
      void set_locality_zone(std::string locality_zone) {
        locality_zone_ = std::move(locality_zone);
      }
      void set_locality_sub_zone(std::string locality_sub_zone) {
        locality_sub_zone_ = std::move(locality_sub_zone);
      }
      void set_metadata(Json::Object metadata) {
        metadata_ = std::move(metadata);
      }

     private:
      std::string id_ = "xds_client_test";
      std::string cluster_;
      std::string locality_region_;
      std::string locality_zone_;
      std::string locality_sub_zone_;
      Json::Object metadata_;
    };

    class FakeXdsServer : public XdsServer {
     public:
      const std::string& server_uri() const override { return server_uri_; }
      bool IgnoreResourceDeletion() const override {
        return ignore_resource_deletion_;
      }
      bool Equals(const XdsServer& other) const override {
        const auto& o = static_cast<const FakeXdsServer&>(other);
        return server_uri_ == o.server_uri_ &&
               ignore_resource_deletion_ == o.ignore_resource_deletion_;
      }

      void set_server_uri(std::string server_uri) {
        server_uri_ = std::move(server_uri);
      }
      void set_ignore_resource_deletion(bool ignore_resource_deletion) {
        ignore_resource_deletion_ = ignore_resource_deletion;
      }

     private:
      std::string server_uri_ = "default_xds_server";
      bool ignore_resource_deletion_ = false;
    };

    class FakeAuthority : public Authority {
     public:
      const XdsServer* server() const override {
        return server_.has_value() ? &*server_ : nullptr;
      }

      void set_server(absl::optional<FakeXdsServer> server) {
        server_ = std::move(server);
      }

     private:
      absl::optional<FakeXdsServer> server_;
    };

    class Builder {
     public:
      Builder() { node_.emplace(); }

      Builder& set_node_id(std::string id) {
        if (!node_.has_value()) node_.emplace();
        node_->set_id(std::move(id));
        return *this;
      }
      Builder& AddAuthority(std::string name, FakeAuthority authority) {
        authorities_[std::move(name)] = std::move(authority);
        return *this;
      }
      Builder& set_ignore_resource_deletion(bool ignore_resource_deletion) {
        server_.set_ignore_resource_deletion(ignore_resource_deletion);
        return *this;
      }
      std::unique_ptr<XdsBootstrap> Build() {
        auto bootstrap = std::make_unique<FakeXdsBootstrap>();
        bootstrap->server_ = std::move(server_);
        bootstrap->node_ = std::move(node_);
        bootstrap->authorities_ = std::move(authorities_);
        return bootstrap;
      }

     private:
      FakeXdsServer server_;
      absl::optional<FakeNode> node_;
      std::map<std::string, FakeAuthority> authorities_;
    };

    std::string ToString() const override { return "<fake>"; }

    const XdsServer& server() const override { return server_; }
    const Node* node() const override {
      return node_.has_value() ? &*node_ : nullptr;
    }
    const Authority* LookupAuthority(const std::string& name) const override {
      auto it = authorities_.find(name);
      if (it == authorities_.end()) return nullptr;
      return &it->second;
    }
    const XdsServer* FindXdsServer(const XdsServer& server) const override {
      const auto& fake_server = static_cast<const FakeXdsServer&>(server);
      if (fake_server == server_) return &server_;
      for (const auto& p : authorities_) {
        const auto* authority_server =
            static_cast<const FakeXdsServer*>(p.second.server());
        if (authority_server != nullptr && *authority_server == fake_server) {
          return authority_server;
        }
      }
      return nullptr;
    }

   private:
    FakeXdsServer server_;
    absl::optional<FakeNode> node_;
    std::map<std::string, FakeAuthority> authorities_;
  };

  // A template for a test xDS resource type with an associated watcher impl.
  // For simplicity, we use JSON instead of proto for serialization.
  //
  // The specified ResourceStruct must provide the following:
  // - a static JsonLoader() method, as described in json_object_loader.h
  // - an AsJsonString() method that returns the object in JSON string form
  // - a static TypeUrl() method that returns the resource type
  //
  // The all_resources_required_in_sotw parameter indicates the value
  // that should be returned by the AllResourcesRequiredInSotW() method.
  template <typename ResourceStruct, bool all_resources_required_in_sotw>
  class XdsTestResourceType
      : public XdsResourceTypeImpl<
            XdsTestResourceType<ResourceStruct, all_resources_required_in_sotw>,
            ResourceStruct> {
   public:
    // A watcher implementation that queues delivered watches.
    class Watcher : public XdsResourceTypeImpl<
                        XdsTestResourceType<ResourceStruct,
                                            all_resources_required_in_sotw>,
                        ResourceStruct>::WatcherInterface {
     public:
      // Returns true if no event is received during the timeout period.
      bool ExpectNoEvent(absl::Duration timeout) {
        MutexLock lock(&mu_);
        return !WaitForEventLocked(timeout);
      }

      bool HasEvent() {
        MutexLock lock(&mu_);
        return !queue_.empty();
      }

      absl::optional<ResourceStruct> WaitForNextResource(
          absl::Duration timeout = absl::Seconds(1),
          SourceLocation location = SourceLocation()) {
        MutexLock lock(&mu_);
        if (!WaitForEventLocked(timeout)) return absl::nullopt;
        Event& event = queue_.front();
        if (!absl::holds_alternative<ResourceStruct>(event)) {
          EXPECT_TRUE(false)
              << "got unexpected event "
              << (absl::holds_alternative<absl::Status>(event)
                      ? "error"
                      : "does-not-exist")
              << " at " << location.file() << ":" << location.line();
          return absl::nullopt;
        }
        ResourceStruct foo = std::move(absl::get<ResourceStruct>(event));
        queue_.pop_front();
        return std::move(foo);
      }

      absl::optional<absl::Status> WaitForNextError(
          absl::Duration timeout = absl::Seconds(1),
          SourceLocation location = SourceLocation()) {
        MutexLock lock(&mu_);
        if (!WaitForEventLocked(timeout)) return absl::nullopt;
        Event& event = queue_.front();
        if (!absl::holds_alternative<absl::Status>(event)) {
          EXPECT_TRUE(false)
              << "got unexpected event "
              << (absl::holds_alternative<ResourceStruct>(event)
                      ? "resource"
                      : "does-not-exist")
              << " at " << location.file() << ":" << location.line();
          return absl::nullopt;
        }
        absl::Status error = std::move(absl::get<absl::Status>(event));
        queue_.pop_front();
        return std::move(error);
      }

      bool WaitForDoesNotExist(absl::Duration timeout,
                               SourceLocation location = SourceLocation()) {
        MutexLock lock(&mu_);
        if (!WaitForEventLocked(timeout)) return false;
        Event& event = queue_.front();
        if (!absl::holds_alternative<DoesNotExist>(event)) {
          EXPECT_TRUE(false)
              << "got unexpected event "
              << (absl::holds_alternative<ResourceStruct>(event) ? "resource"
                                                                 : "error")
              << " at " << location.file() << ":" << location.line();
          return false;
        }
        queue_.pop_front();
        return true;
      }

     private:
      struct DoesNotExist {};
      using Event = absl::variant<ResourceStruct, absl::Status, DoesNotExist>;

      void OnResourceChanged(ResourceStruct foo) override {
        MutexLock lock(&mu_);
        queue_.push_back(std::move(foo));
        cv_.Signal();
      }
      void OnError(absl::Status status) override {
        MutexLock lock(&mu_);
        queue_.push_back(std::move(status));
        cv_.Signal();
      }
      void OnResourceDoesNotExist() override {
        MutexLock lock(&mu_);
        queue_.push_back(DoesNotExist());
        cv_.Signal();
      }

      // Returns true if an event was received, or false if the timeout
      // expires before any event is received.
      bool WaitForEventLocked(absl::Duration timeout)
          ABSL_EXCLUSIVE_LOCKS_REQUIRED(&mu_) {
        while (queue_.empty()) {
          if (cv_.WaitWithTimeout(&mu_,
                                  timeout * grpc_test_slowdown_factor())) {
            return false;
          }
        }
        return true;
      }

      Mutex mu_;
      CondVar cv_;
      std::deque<Event> queue_ ABSL_GUARDED_BY(&mu_);
    };

    absl::string_view type_url() const override {
      return ResourceStruct::TypeUrl();
    }
    XdsResourceType::DecodeResult Decode(
        const XdsResourceType::DecodeContext& /*context*/,
        absl::string_view serialized_resource) const override {
      auto json = Json::Parse(serialized_resource);
      XdsResourceType::DecodeResult result;
      if (!json.ok()) {
        result.resource = json.status();
      } else {
        absl::StatusOr<ResourceStruct> foo =
            LoadFromJson<ResourceStruct>(*json);
        if (!foo.ok()) {
          auto it = json->object_value().find("name");
          if (it != json->object_value().end()) {
            result.name = it->second.string_value();
          }
          result.resource = foo.status();
        } else {
          result.name = foo->name;
          result.resource = std::make_unique<ResourceStruct>(std::move(*foo));
        }
      }
      return result;
    }
    bool AllResourcesRequiredInSotW() const override {
      return all_resources_required_in_sotw;
    }
    void InitUpbSymtab(XdsClient*, upb_DefPool* /*symtab*/) const override {}

    static google::protobuf::Any EncodeAsAny(const ResourceStruct& resource) {
      google::protobuf::Any any;
      any.set_type_url(
          absl::StrCat("type.googleapis.com/", ResourceStruct::TypeUrl()));
      any.set_value(resource.AsJsonString());
      return any;
    }
  };

  // A fake "Foo" xDS resource type.
  struct XdsFooResource : public XdsResourceType::ResourceData {
    std::string name;
    uint32_t value;

    XdsFooResource() = default;
    XdsFooResource(std::string name, uint32_t value)
        : name(std::move(name)), value(value) {}

    bool operator==(const XdsFooResource& other) const {
      return name == other.name && value == other.value;
    }

    std::string AsJsonString() const {
      return absl::StrCat("{\"name\":\"", name, "\",\"value\":", value, "}");
    }

    static const JsonLoaderInterface* JsonLoader(const JsonArgs&) {
      static const auto* loader = JsonObjectLoader<XdsFooResource>()
                                      .Field("name", &XdsFooResource::name)
                                      .Field("value", &XdsFooResource::value)
                                      .Finish();
      return loader;
    }

    static absl::string_view TypeUrl() { return "test.v3.foo"; }
  };
  using XdsFooResourceType = XdsTestResourceType<XdsFooResource, false>;

  // A fake "Bar" xDS resource type.
  struct XdsBarResource : public XdsResourceType::ResourceData {
    std::string name;
    std::string value;

    XdsBarResource() = default;
    XdsBarResource(std::string name, std::string value)
        : name(std::move(name)), value(std::move(value)) {}

    bool operator==(const XdsBarResource& other) const {
      return name == other.name && value == other.value;
    }

    std::string AsJsonString() const {
      return absl::StrCat("{\"name\":\"", name, "\",\"value\":\"", value,
                          "\"}");
    }

    static const JsonLoaderInterface* JsonLoader(const JsonArgs&) {
      static const auto* loader = JsonObjectLoader<XdsBarResource>()
                                      .Field("name", &XdsBarResource::name)
                                      .Field("value", &XdsBarResource::value)
                                      .Finish();
      return loader;
    }

    static absl::string_view TypeUrl() { return "test.v3.bar"; }
  };
  using XdsBarResourceType = XdsTestResourceType<XdsBarResource, false>;

  // A fake "WildcardCapable" xDS resource type.
  // This resource type return true for AllResourcesRequiredInSotW(),
  // just like LDS and CDS.
  struct XdsWildcardCapableResource : public XdsResourceType::ResourceData {
    std::string name;
    uint32_t value;

    XdsWildcardCapableResource() = default;
    XdsWildcardCapableResource(std::string name, uint32_t value)
        : name(std::move(name)), value(value) {}

    bool operator==(const XdsWildcardCapableResource& other) const {
      return name == other.name && value == other.value;
    }

    std::string AsJsonString() const {
      return absl::StrCat("{\"name\":\"", name, "\",\"value\":\"", value,
                          "\"}");
    }

    static const JsonLoaderInterface* JsonLoader(const JsonArgs&) {
      static const auto* loader =
          JsonObjectLoader<XdsWildcardCapableResource>()
              .Field("name", &XdsWildcardCapableResource::name)
              .Field("value", &XdsWildcardCapableResource::value)
              .Finish();
      return loader;
    }

    static absl::string_view TypeUrl() { return "test.v3.wildcard_capable"; }
  };
  using XdsWildcardCapableResourceType =
      XdsTestResourceType<XdsWildcardCapableResource,
                          /*all_resources_required_in_sotw=*/true>;

  // A helper class to build and serialize a DiscoveryResponse.
  class ResponseBuilder {
   public:
    explicit ResponseBuilder(absl::string_view type_url) {
      response_.set_type_url(absl::StrCat("type.googleapis.com/", type_url));
    }

    ResponseBuilder& set_version_info(absl::string_view version_info) {
      response_.set_version_info(std::string(version_info));
      return *this;
    }
    ResponseBuilder& set_nonce(absl::string_view nonce) {
      response_.set_nonce(std::string(nonce));
      return *this;
    }

    template <typename ResourceType>
    ResponseBuilder& AddResource(
        const typename ResourceType::ResourceType& resource,
        bool in_resource_wrapper = false) {
      auto* res = response_.add_resources();
      *res = ResourceType::EncodeAsAny(resource);
      if (in_resource_wrapper) {
        envoy::service::discovery::v3::Resource resource_wrapper;
        resource_wrapper.set_name(resource.name);
        *resource_wrapper.mutable_resource() = std::move(*res);
        res->PackFrom(resource_wrapper);
      }
      return *this;
    }

    ResponseBuilder& AddFooResource(const XdsFooResource& resource,
                                    bool in_resource_wrapper = false) {
      return AddResource<XdsFooResourceType>(resource, in_resource_wrapper);
    }

    ResponseBuilder& AddBarResource(const XdsBarResource& resource,
                                    bool in_resource_wrapper = false) {
      return AddResource<XdsBarResourceType>(resource, in_resource_wrapper);
    }

    ResponseBuilder& AddWildcardCapableResource(
        const XdsWildcardCapableResource& resource,
        bool in_resource_wrapper = false) {
      return AddResource<XdsWildcardCapableResourceType>(resource,
                                                         in_resource_wrapper);
    }

    ResponseBuilder& AddInvalidResource(
        absl::string_view type_url, absl::string_view value,
        absl::string_view resource_wrapper_name = "") {
      auto* res = response_.add_resources();
      res->set_type_url(absl::StrCat("type.googleapis.com/", type_url));
      res->set_value(std::string(value));
      if (!resource_wrapper_name.empty()) {
        envoy::service::discovery::v3::Resource resource_wrapper;
        resource_wrapper.set_name(std::string(resource_wrapper_name));
        *resource_wrapper.mutable_resource() = std::move(*res);
        res->PackFrom(resource_wrapper);
      }
      return *this;
    }

    ResponseBuilder& AddEmptyResource() {
      response_.add_resources();
      return *this;
    }

    std::string Serialize() {
      std::string serialized_response;
      EXPECT_TRUE(response_.SerializeToString(&serialized_response));
      return serialized_response;
    }

   private:
    DiscoveryResponse response_;
  };

  // Sets transport_factory_ and initializes xds_client_ with the
  // specified bootstrap config.
  void InitXdsClient(
      FakeXdsBootstrap::Builder bootstrap_builder = FakeXdsBootstrap::Builder(),
      Duration resource_request_timeout = Duration::Seconds(15)) {
    auto transport_factory = MakeOrphanable<FakeXdsTransportFactory>();
    transport_factory_ = transport_factory->Ref();
    xds_client_ = MakeRefCounted<XdsClient>(
        bootstrap_builder.Build(), std::move(transport_factory),
        grpc_event_engine::experimental::GetDefaultEventEngine(), "foo agent",
        "foo version", resource_request_timeout * grpc_test_slowdown_factor());
  }

  // Starts and cancels a watch for a Foo resource.
  RefCountedPtr<XdsFooResourceType::Watcher> StartFooWatch(
      absl::string_view resource_name) {
    auto watcher = MakeRefCounted<XdsFooResourceType::Watcher>();
    XdsFooResourceType::StartWatch(xds_client_.get(), resource_name, watcher);
    return watcher;
  }
  void CancelFooWatch(XdsFooResourceType::Watcher* watcher,
                      absl::string_view resource_name,
                      bool delay_unsubscription = false) {
    XdsFooResourceType::CancelWatch(xds_client_.get(), resource_name, watcher,
                                    delay_unsubscription);
  }

  // Starts and cancels a watch for a Bar resource.
  RefCountedPtr<XdsBarResourceType::Watcher> StartBarWatch(
      absl::string_view resource_name) {
    auto watcher = MakeRefCounted<XdsBarResourceType::Watcher>();
    XdsBarResourceType::StartWatch(xds_client_.get(), resource_name, watcher);
    return watcher;
  }
  void CancelBarWatch(XdsBarResourceType::Watcher* watcher,
                      absl::string_view resource_name,
                      bool delay_unsubscription = false) {
    XdsBarResourceType::CancelWatch(xds_client_.get(), resource_name, watcher,
                                    delay_unsubscription);
  }

  // Starts and cancels a watch for a WildcardCapable resource.
  RefCountedPtr<XdsWildcardCapableResourceType::Watcher>
  StartWildcardCapableWatch(absl::string_view resource_name) {
    auto watcher = MakeRefCounted<XdsWildcardCapableResourceType::Watcher>();
    XdsWildcardCapableResourceType::StartWatch(xds_client_.get(), resource_name,
                                               watcher);
    return watcher;
  }
  void CancelWildcardCapableWatch(
      XdsWildcardCapableResourceType::Watcher* watcher,
      absl::string_view resource_name, bool delay_unsubscription = false) {
    XdsWildcardCapableResourceType::CancelWatch(
        xds_client_.get(), resource_name, watcher, delay_unsubscription);
  }

  RefCountedPtr<FakeXdsTransportFactory::FakeStreamingCall> WaitForAdsStream(
      const XdsBootstrap::XdsServer& server,
      absl::Duration timeout = absl::Seconds(5)) {
    const auto* xds_server = xds_client_->bootstrap().FindXdsServer(server);
    GPR_ASSERT(xds_server != nullptr);
    return transport_factory_->WaitForStream(
        *xds_server, FakeXdsTransportFactory::kAdsMethod,
        timeout * grpc_test_slowdown_factor());
  }

  void TriggerConnectionFailure(const XdsBootstrap::XdsServer& server,
                                absl::Status status) {
    const auto* xds_server = xds_client_->bootstrap().FindXdsServer(server);
    GPR_ASSERT(xds_server != nullptr);
    transport_factory_->TriggerConnectionFailure(*xds_server,
                                                 std::move(status));
  }

  RefCountedPtr<FakeXdsTransportFactory::FakeStreamingCall> WaitForAdsStream(
      absl::Duration timeout = absl::Seconds(5)) {
    return WaitForAdsStream(xds_client_->bootstrap().server(), timeout);
  }

  // Gets the latest request sent to the fake xDS server.
  absl::optional<DiscoveryRequest> WaitForRequest(
      FakeXdsTransportFactory::FakeStreamingCall* stream,
      absl::Duration timeout = absl::Seconds(3),
      SourceLocation location = SourceLocation()) {
    auto message =
        stream->WaitForMessageFromClient(timeout * grpc_test_slowdown_factor());
    if (!message.has_value()) return absl::nullopt;
    DiscoveryRequest request;
    bool success = request.ParseFromString(*message);
    EXPECT_TRUE(success) << "Failed to deserialize DiscoveryRequest at "
                         << location.file() << ":" << location.line();
    if (!success) return absl::nullopt;
    return std::move(request);
  }

  // Helper function to check the fields of a DiscoveryRequest.
  void CheckRequest(const DiscoveryRequest& request, absl::string_view type_url,
                    absl::string_view version_info,
                    absl::string_view response_nonce, absl::Status error_detail,
                    std::set<absl::string_view> resource_names,
                    SourceLocation location = SourceLocation()) {
    EXPECT_EQ(request.type_url(),
              absl::StrCat("type.googleapis.com/", type_url))
        << location.file() << ":" << location.line();
    EXPECT_EQ(request.version_info(), version_info)
        << location.file() << ":" << location.line();
    EXPECT_EQ(request.response_nonce(), response_nonce)
        << location.file() << ":" << location.line();
    if (error_detail.ok()) {
      EXPECT_FALSE(request.has_error_detail())
          << location.file() << ":" << location.line();
    } else {
      EXPECT_EQ(request.error_detail().code(),
                static_cast<int>(error_detail.code()))
          << location.file() << ":" << location.line();
      EXPECT_EQ(request.error_detail().message(), error_detail.message())
          << location.file() << ":" << location.line();
    }
    EXPECT_THAT(request.resource_names(),
                ::testing::UnorderedElementsAreArray(resource_names))
        << location.file() << ":" << location.line();
  }

  // Helper function to check the contents of the node message in a
  // request against the client's node info.
  void CheckRequestNode(const DiscoveryRequest& request,
                        SourceLocation location = SourceLocation()) {
    // These fields come from the bootstrap config.
    EXPECT_EQ(request.node().id(), xds_client_->bootstrap().node()->id())
        << location.file() << ":" << location.line();
    EXPECT_EQ(request.node().cluster(),
              xds_client_->bootstrap().node()->cluster())
        << location.file() << ":" << location.line();
    EXPECT_EQ(request.node().locality().region(),
              xds_client_->bootstrap().node()->locality_region())
        << location.file() << ":" << location.line();
    EXPECT_EQ(request.node().locality().zone(),
              xds_client_->bootstrap().node()->locality_zone())
        << location.file() << ":" << location.line();
    EXPECT_EQ(request.node().locality().sub_zone(),
              xds_client_->bootstrap().node()->locality_sub_zone())
        << location.file() << ":" << location.line();
    if (xds_client_->bootstrap().node()->metadata().empty()) {
      EXPECT_FALSE(request.node().has_metadata())
          << location.file() << ":" << location.line();
    } else {
      std::string metadata_json_str;
      auto status =
          MessageToJsonString(request.node().metadata(), &metadata_json_str,
                              GRPC_CUSTOM_JSONUTIL::JsonPrintOptions());
      ASSERT_TRUE(status.ok())
          << status << " on " << location.file() << ":" << location.line();
      auto metadata_json = Json::Parse(metadata_json_str);
      ASSERT_TRUE(metadata_json.ok())
          << metadata_json.status() << " on " << location.file() << ":"
          << location.line();
      EXPECT_EQ(*metadata_json, xds_client_->bootstrap().node()->metadata())
          << location.file() << ":" << location.line() << ":\nexpected: "
          << Json{xds_client_->bootstrap().node()->metadata()}.Dump()
          << "\nactual: " << metadata_json->Dump();
    }
    EXPECT_EQ(request.node().user_agent_name(), "foo agent")
        << location.file() << ":" << location.line();
    EXPECT_EQ(request.node().user_agent_version(), "foo version")
        << location.file() << ":" << location.line();
  }

  RefCountedPtr<FakeXdsTransportFactory> transport_factory_;
  RefCountedPtr<XdsClient> xds_client_;
};

TEST_F(XdsClientTest, BasicWatch) {
  InitXdsClient();
  // Start a watch for "foo1".
  auto watcher = StartFooWatch("foo1");
  // Watcher should initially not see any resource reported.
  EXPECT_FALSE(watcher->HasEvent());
  // XdsClient should have created an ADS stream.
  auto stream = WaitForAdsStream();
  ASSERT_TRUE(stream != nullptr);
  // XdsClient should have sent a subscription request on the ADS stream.
  auto request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  CheckRequestNode(*request);  // Should be present on the first request.
  // Send a response.
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("1")
          .set_nonce("A")
          .AddFooResource(XdsFooResource("foo1", 6))
          .Serialize());
  // XdsClient should have delivered the response to the watcher.
  auto resource = watcher->WaitForNextResource();
  ASSERT_TRUE(resource.has_value());
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  // Cancel watch.
  CancelFooWatch(watcher.get(), "foo1");
  EXPECT_TRUE(stream->Orphaned());
}

TEST_F(XdsClientTest, UpdateFromServer) {
  InitXdsClient();
  // Start a watch for "foo1".
  auto watcher = StartFooWatch("foo1");
  // Watcher should initially not see any resource reported.
  EXPECT_FALSE(watcher->HasEvent());
  // XdsClient should have created an ADS stream.
  auto stream = WaitForAdsStream();
  ASSERT_TRUE(stream != nullptr);
  // XdsClient should have sent a subscription request on the ADS stream.
  auto request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  CheckRequestNode(*request);  // Should be present on the first request.
  // Send a response.
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("1")
          .set_nonce("A")
          .AddFooResource(XdsFooResource("foo1", 6))
          .Serialize());
  // XdsClient should have delivered the response to the watcher.
  auto resource = watcher->WaitForNextResource();
  ASSERT_TRUE(resource.has_value());
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  // Server sends an updated version of the resource.
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("2")
          .set_nonce("B")
          .AddFooResource(XdsFooResource("foo1", 9))
          .Serialize());
  // XdsClient should have delivered the response to the watcher.
  resource = watcher->WaitForNextResource();
  ASSERT_TRUE(resource.has_value());
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 9);
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"2", /*response_nonce=*/"B",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  // Cancel watch.
  CancelFooWatch(watcher.get(), "foo1");
  EXPECT_TRUE(stream->Orphaned());
}

TEST_F(XdsClientTest, MultipleWatchersForSameResource) {
  InitXdsClient();
  // Start a watch for "foo1".
  auto watcher = StartFooWatch("foo1");
  // Watcher should initially not see any resource reported.
  EXPECT_FALSE(watcher->HasEvent());
  // XdsClient should have created an ADS stream.
  auto stream = WaitForAdsStream();
  ASSERT_TRUE(stream != nullptr);
  // XdsClient should have sent a subscription request on the ADS stream.
  auto request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  CheckRequestNode(*request);  // Should be present on the first request.
  // Send a response.
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("1")
          .set_nonce("A")
          .AddFooResource(XdsFooResource("foo1", 6))
          .Serialize());
  // XdsClient should have delivered the response to the watcher.
  auto resource = watcher->WaitForNextResource();
  ASSERT_TRUE(resource.has_value());
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  // Start a second watcher for the same resource.
  auto watcher2 = StartFooWatch("foo1");
  // This watcher should get an immediate notification, because the
  // resource is already cached.
  resource = watcher2->WaitForNextResource();
  ASSERT_TRUE(resource.has_value());
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  // Server should not have seen another request from the client.
  ASSERT_FALSE(stream->HaveMessageFromClient());
  // Server sends an updated version of the resource.
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("2")
          .set_nonce("B")
          .AddFooResource(XdsFooResource("foo1", 9))
          .Serialize());
  // XdsClient should deliver the response to both watchers.
  resource = watcher->WaitForNextResource();
  ASSERT_TRUE(resource.has_value());
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 9);
  resource = watcher2->WaitForNextResource();
  ASSERT_TRUE(resource.has_value());
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 9);
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"2", /*response_nonce=*/"B",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  // Cancel one of the watchers.
  CancelFooWatch(watcher.get(), "foo1");
  // The server should not see any new request.
  ASSERT_FALSE(WaitForRequest(stream.get()));
  // Now cancel the second watcher.
  CancelFooWatch(watcher2.get(), "foo1");
  EXPECT_TRUE(stream->Orphaned());
}

TEST_F(XdsClientTest, SubscribeToMultipleResources) {
  InitXdsClient();
  // Start a watch for "foo1".
  auto watcher = StartFooWatch("foo1");
  // Watcher should initially not see any resource reported.
  EXPECT_FALSE(watcher->HasEvent());
  // XdsClient should have created an ADS stream.
  auto stream = WaitForAdsStream();
  ASSERT_TRUE(stream != nullptr);
  // XdsClient should have sent a subscription request on the ADS stream.
  auto request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  CheckRequestNode(*request);  // Should be present on the first request.
  // Send a response.
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("1")
          .set_nonce("A")
          .AddFooResource(XdsFooResource("foo1", 6))
          .Serialize());
  // XdsClient should have delivered the response to the watcher.
  auto resource = watcher->WaitForNextResource();
  ASSERT_TRUE(resource.has_value());
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  // Start a watch for "foo2".
  auto watcher2 = StartFooWatch("foo2");
  // XdsClient should have sent a subscription request on the ADS stream.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1", "foo2"});
  // Send a response.
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("1")
          .set_nonce("B")
          .AddFooResource(XdsFooResource("foo2", 7))
          .Serialize());
  // XdsClient should have delivered the response to the watcher.
  resource = watcher2->WaitForNextResource();
  ASSERT_TRUE(resource.has_value());
  EXPECT_EQ(resource->name, "foo2");
  EXPECT_EQ(resource->value, 7);
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"B",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1", "foo2"});
  // Cancel watch for "foo1".
  CancelFooWatch(watcher.get(), "foo1");
  // XdsClient should send an unsubscription request.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"B",
               /*error_detail=*/absl::OkStatus(), /*resource_names=*/{"foo2"});
  // Now cancel watch for "foo2".
  CancelFooWatch(watcher2.get(), "foo2");
  EXPECT_TRUE(stream->Orphaned());
}

TEST_F(XdsClientTest, UpdateContainsOnlyChangedResource) {
  InitXdsClient();
  // Start a watch for "foo1".
  auto watcher = StartFooWatch("foo1");
  // Watcher should initially not see any resource reported.
  EXPECT_FALSE(watcher->HasEvent());
  // XdsClient should have created an ADS stream.
  auto stream = WaitForAdsStream();
  ASSERT_TRUE(stream != nullptr);
  // XdsClient should have sent a subscription request on the ADS stream.
  auto request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  CheckRequestNode(*request);  // Should be present on the first request.
  // Send a response.
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("1")
          .set_nonce("A")
          .AddFooResource(XdsFooResource("foo1", 6))
          .Serialize());
  // XdsClient should have delivered the response to the watcher.
  auto resource = watcher->WaitForNextResource();
  ASSERT_TRUE(resource.has_value());
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  // Start a watch for "foo2".
  auto watcher2 = StartFooWatch("foo2");
  // XdsClient should have sent a subscription request on the ADS stream.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1", "foo2"});
  // Send a response.
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("1")
          .set_nonce("B")
          .AddFooResource(XdsFooResource("foo2", 7))
          .Serialize());
  // XdsClient should have delivered the response to the watcher.
  resource = watcher2->WaitForNextResource();
  ASSERT_TRUE(resource.has_value());
  EXPECT_EQ(resource->name, "foo2");
  EXPECT_EQ(resource->value, 7);
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"B",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1", "foo2"});
  // Server sends an update for "foo1".  The response does not contain "foo2".
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("2")
          .set_nonce("C")
          .AddFooResource(XdsFooResource("foo1", 9))
          .Serialize());
  // XdsClient should have delivered the response to the watcher.
  resource = watcher->WaitForNextResource();
  ASSERT_TRUE(resource.has_value());
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 9);
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"2", /*response_nonce=*/"C",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1", "foo2"});
  // Cancel watch for "foo1".
  CancelFooWatch(watcher.get(), "foo1");
  // XdsClient should send an unsubscription request.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"2", /*response_nonce=*/"C",
               /*error_detail=*/absl::OkStatus(), /*resource_names=*/{"foo2"});
  // Now cancel watch for "foo2".
  CancelFooWatch(watcher2.get(), "foo2");
  EXPECT_TRUE(stream->Orphaned());
}

TEST_F(XdsClientTest, ResourceValidationFailure) {
  InitXdsClient();
  // Start a watch for "foo1".
  auto watcher = StartFooWatch("foo1");
  // Watcher should initially not see any resource reported.
  EXPECT_FALSE(watcher->HasEvent());
  // XdsClient should have created an ADS stream.
  auto stream = WaitForAdsStream();
  ASSERT_TRUE(stream != nullptr);
  // XdsClient should have sent a subscription request on the ADS stream.
  auto request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  CheckRequestNode(*request);  // Should be present on the first request.
  // Send a response containing an invalid resource.
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("1")
          .set_nonce("A")
          .AddInvalidResource(XdsFooResourceType::Get()->type_url(),
                              "{\"name\":\"foo1\",\"value\":[]}")
          .Serialize());
  // XdsClient should deliver an error to the watcher.
  auto error = watcher->WaitForNextError();
  ASSERT_TRUE(error.has_value());
  EXPECT_EQ(error->code(), absl::StatusCode::kUnavailable);
  EXPECT_EQ(error->message(),
            "invalid resource: INVALID_ARGUMENT: errors validating JSON: "
            "[field:value error:is not a number] (node ID:xds_client_test)")
      << *error;
  // XdsClient should NACK the update.
  // Note that version_info is not populated in the request.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(
      *request, XdsFooResourceType::Get()->type_url(),
      /*version_info=*/"", /*response_nonce=*/"A",
      // error_detail=
      absl::InvalidArgumentError(
          "xDS response validation errors: ["
          "resource index 0: foo1: INVALID_ARGUMENT: errors validating JSON: "
          "[field:value error:is not a number]]"),
      /*resource_names=*/{"foo1"});
  // Start a second watch for the same resource.  It should immediately
  // receive the same error.
  auto watcher2 = StartFooWatch("foo1");
  error = watcher2->WaitForNextError();
  ASSERT_TRUE(error.has_value());
  EXPECT_EQ(error->code(), absl::StatusCode::kUnavailable);
  EXPECT_EQ(error->message(),
            "invalid resource: INVALID_ARGUMENT: errors validating JSON: "
            "[field:value error:is not a number] (node ID:xds_client_test)")
      << *error;
  // Now server sends an updated version of the resource.
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("2")
          .set_nonce("B")
          .AddFooResource(XdsFooResource("foo1", 9))
          .Serialize());
  // XdsClient should deliver the response to both watchers.
  auto resource = watcher->WaitForNextResource();
  ASSERT_TRUE(resource.has_value());
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 9);
  resource = watcher2->WaitForNextResource();
  ASSERT_TRUE(resource.has_value());
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 9);
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"2", /*response_nonce=*/"B",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  // Cancel watch.
  CancelFooWatch(watcher.get(), "foo1");
  CancelFooWatch(watcher2.get(), "foo1");
  EXPECT_TRUE(stream->Orphaned());
}

TEST_F(XdsClientTest, ResourceValidationFailureMultipleResources) {
  InitXdsClient();
  // Start a watch for "foo1".
  auto watcher = StartFooWatch("foo1");
  // Watcher should initially not see any resource reported.
  EXPECT_FALSE(watcher->HasEvent());
  // XdsClient should have created an ADS stream.
  auto stream = WaitForAdsStream();
  ASSERT_TRUE(stream != nullptr);
  // XdsClient should have sent a subscription request on the ADS stream.
  auto request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  CheckRequestNode(*request);  // Should be present on the first request.
  // Before the server responds, add a watch for another resource.
  auto watcher2 = StartFooWatch("foo2");
  // Client should send another request.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1", "foo2"});
  // Add a watch for a third resource.
  auto watcher3 = StartFooWatch("foo3");
  // Client should send another request.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1", "foo2", "foo3"});
  // Add a watch for a fourth resource.
  auto watcher4 = StartFooWatch("foo4");
  // Client should send another request.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1", "foo2", "foo3", "foo4"});
  // Server sends a response containing three invalid resources and one
  // valid resource.
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("1")
          .set_nonce("A")
          // foo1: JSON parsing succeeds, so we know the resource name,
          // but validation fails.
          .AddInvalidResource(XdsFooResourceType::Get()->type_url(),
                              "{\"name\":\"foo1\",\"value\":[]}")
          // foo2: JSON parsing fails, and not wrapped in a Resource
          // wrapper, so we don't actually know the resource's name.
          .AddInvalidResource(XdsFooResourceType::Get()->type_url(),
                              "{\"name\":\"foo2,\"value\":6}")
          // Empty resource.  Will be included in NACK but will not
          // affect any watchers.
          .AddEmptyResource()
          // foo3: JSON parsing fails, but it is wrapped in a Resource
          // wrapper, so we do know the resource's name.
          .AddInvalidResource(XdsFooResourceType::Get()->type_url(),
                              "{\"name\":\"foo3,\"value\":6}",
                              /*resource_wrapper_name=*/"foo3")
          // foo4: valid resource.
          .AddFooResource(XdsFooResource("foo4", 5))
          .Serialize());
  // XdsClient should deliver an error to the watchers for foo1 and foo3.
  auto error = watcher->WaitForNextError();
  ASSERT_TRUE(error.has_value());
  EXPECT_EQ(error->code(), absl::StatusCode::kUnavailable);
  EXPECT_EQ(error->message(),
            "invalid resource: INVALID_ARGUMENT: errors validating JSON: "
            "[field:value error:is not a number] (node ID:xds_client_test)")
      << *error;
  error = watcher3->WaitForNextError();
  ASSERT_TRUE(error.has_value());
  EXPECT_EQ(error->code(), absl::StatusCode::kUnavailable);
  EXPECT_EQ(error->message(),
            "invalid resource: INVALID_ARGUMENT: JSON parsing failed: "
            "[JSON parse error at index 15] (node ID:xds_client_test)")
      << *error;
  // It cannot delivery an error for foo2, because the client doesn't know
  // that that resource in the response was actually supposed to be foo2.
  EXPECT_FALSE(watcher2->HasEvent());
  // It will delivery a valid resource update for foo4.
  auto resource = watcher4->WaitForNextResource();
  ASSERT_TRUE(resource.has_value());
  EXPECT_EQ(resource->name, "foo4");
  EXPECT_EQ(resource->value, 5);
  // XdsClient should NACK the update.
  // There was one good resource, so the version will be updated.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               // error_detail=
               absl::InvalidArgumentError(absl::StrCat(
                   "xDS response validation errors: ["
                   // foo1
                   "resource index 0: foo1: "
                   "INVALID_ARGUMENT: errors validating JSON: "
                   "[field:value error:is not a number]; "
                   // foo2 (name not known)
                   "resource index 1: INVALID_ARGUMENT: JSON parsing failed: "
                   "[JSON parse error at index 15]; "
                   // empty resource
                   "resource index 2: incorrect resource type \"\" "
                   "(should be \"",
                   XdsFooResourceType::Get()->type_url(),
                   "\"); "
                   // foo3
                   "resource index 3: foo3: "
                   "INVALID_ARGUMENT: JSON parsing failed: "
                   "[JSON parse error at index 15]]")),
               /*resource_names=*/{"foo1", "foo2", "foo3", "foo4"});
  // Cancel watches.
  CancelFooWatch(watcher.get(), "foo1", /*delay_unsubscription=*/true);
  CancelFooWatch(watcher2.get(), "foo2", /*delay_unsubscription=*/true);
  CancelFooWatch(watcher3.get(), "foo3", /*delay_unsubscription=*/true);
  CancelFooWatch(watcher4.get(), "foo4");
  EXPECT_TRUE(stream->Orphaned());
}

TEST_F(XdsClientTest, ResourceValidationFailureForCachedResource) {
  InitXdsClient();
  // Start a watch for "foo1".
  auto watcher = StartFooWatch("foo1");
  // Watcher should initially not see any resource reported.
  EXPECT_FALSE(watcher->HasEvent());
  // XdsClient should have created an ADS stream.
  auto stream = WaitForAdsStream();
  ASSERT_TRUE(stream != nullptr);
  // XdsClient should have sent a subscription request on the ADS stream.
  auto request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  CheckRequestNode(*request);  // Should be present on the first request.
  // Send a response.
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("1")
          .set_nonce("A")
          .AddFooResource(XdsFooResource("foo1", 6))
          .Serialize());
  // XdsClient should have delivered the response to the watcher.
  auto resource = watcher->WaitForNextResource();
  ASSERT_TRUE(resource.has_value());
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  // Send an update containing an invalid resource.
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("2")
          .set_nonce("B")
          .AddInvalidResource(XdsFooResourceType::Get()->type_url(),
                              "{\"name\":\"foo1\",\"value\":[]}")
          .Serialize());
  // XdsClient should deliver an error to the watcher.
  auto error = watcher->WaitForNextError();
  ASSERT_TRUE(error.has_value());
  EXPECT_EQ(error->code(), absl::StatusCode::kUnavailable);
  EXPECT_EQ(error->message(),
            "invalid resource: INVALID_ARGUMENT: errors validating JSON: "
            "[field:value error:is not a number] (node ID:xds_client_test)")
      << *error;
  // XdsClient should NACK the update.
  // Note that version_info is set to the previous version in this request,
  // because there were no valid resources in it.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(
      *request, XdsFooResourceType::Get()->type_url(),
      /*version_info=*/"1", /*response_nonce=*/"B",
      // error_detail=
      absl::InvalidArgumentError(
          "xDS response validation errors: ["
          "resource index 0: foo1: INVALID_ARGUMENT: errors validating JSON: "
          "[field:value error:is not a number]]"),
      /*resource_names=*/{"foo1"});
  // Start a second watcher for the same resource.  Even though the last
  // update was a NACK, we should still deliver the cached resource to
  // the watcher.
  // TODO(roth): Consider what the right behavior is here.  It seems
  // inconsistent that the watcher sees the error if it had started
  // before the error was seen but does not if it was started afterwards.
  // One option is to not send errors at all for already-cached resources;
  // another option is to send the errors even for newly started watchers.
  auto watcher2 = StartFooWatch("foo1");
  resource = watcher2->WaitForNextResource();
  ASSERT_TRUE(resource.has_value());
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  // Cancel watches.
  CancelFooWatch(watcher.get(), "foo1");
  CancelFooWatch(watcher2.get(), "foo1");
  EXPECT_TRUE(stream->Orphaned());
}

TEST_F(XdsClientTest, WildcardCapableResponseWithEmptyResource) {
  InitXdsClient();
  // Start a watch for "wc1".
  auto watcher = StartWildcardCapableWatch("wc1");
  // Watcher should initially not see any resource reported.
  EXPECT_FALSE(watcher->HasEvent());
  // XdsClient should have created an ADS stream.
  auto stream = WaitForAdsStream();
  ASSERT_TRUE(stream != nullptr);
  // XdsClient should have sent a subscription request on the ADS stream.
  auto request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsWildcardCapableResourceType::Get()->type_url(),
               /*version_info=*/"", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"wc1"});
  CheckRequestNode(*request);  // Should be present on the first request.
  // Server sends a response containing the requested resources plus an
  // empty resource.
  stream->SendMessageToClient(
      ResponseBuilder(XdsWildcardCapableResourceType::Get()->type_url())
          .set_version_info("1")
          .set_nonce("A")
          .AddWildcardCapableResource(XdsWildcardCapableResource("wc1", 6))
          .AddEmptyResource()
          .Serialize());
  // XdsClient will delivery a valid resource update for wc1.
  auto resource = watcher->WaitForNextResource();
  ASSERT_TRUE(resource.has_value());
  EXPECT_EQ(resource->name, "wc1");
  EXPECT_EQ(resource->value, 6);
  // XdsClient should NACK the update.
  // There was one good resource, so the version will be updated.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsWildcardCapableResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               // error_detail=
               absl::InvalidArgumentError(absl::StrCat(
                   "xDS response validation errors: ["
                   "resource index 1: incorrect resource type \"\" "
                   "(should be \"",
                   XdsWildcardCapableResourceType::Get()->type_url(), "\")]")),
               /*resource_names=*/{"wc1"});
  // Cancel watch.
  CancelWildcardCapableWatch(watcher.get(), "wc1");
  EXPECT_TRUE(stream->Orphaned());
}

// This tests resource removal triggered by the server when using a
// resource type that requires all resources to be present in every
// response, similar to LDS and CDS.
TEST_F(XdsClientTest, ResourceDeletion) {
  InitXdsClient();
  // Start a watch for "wc1".
  auto watcher = StartWildcardCapableWatch("wc1");
  // Watcher should initially not see any resource reported.
  EXPECT_FALSE(watcher->HasEvent());
  // XdsClient should have created an ADS stream.
  auto stream = WaitForAdsStream();
  ASSERT_TRUE(stream != nullptr);
  // XdsClient should have sent a subscription request on the ADS stream.
  auto request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsWildcardCapableResourceType::Get()->type_url(),
               /*version_info=*/"", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"wc1"});
  CheckRequestNode(*request);  // Should be present on the first request.
  // Server sends a response.
  stream->SendMessageToClient(
      ResponseBuilder(XdsWildcardCapableResourceType::Get()->type_url())
          .set_version_info("1")
          .set_nonce("A")
          .AddWildcardCapableResource(XdsWildcardCapableResource("wc1", 6))
          .Serialize());
  // XdsClient should have delivered the response to the watcher.
  auto resource = watcher->WaitForNextResource();
  ASSERT_TRUE(resource.has_value());
  EXPECT_EQ(resource->name, "wc1");
  EXPECT_EQ(resource->value, 6);
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsWildcardCapableResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"wc1"});
  // Server now sends a response without the resource, thus indicating
  // it's been deleted.
  stream->SendMessageToClient(
      ResponseBuilder(XdsWildcardCapableResourceType::Get()->type_url())
          .set_version_info("2")
          .set_nonce("B")
          .Serialize());
  // Watcher should see the does-not-exist event.
  EXPECT_TRUE(watcher->WaitForDoesNotExist(absl::Seconds(1)));
  // Start a new watcher for the same resource.  It should immediately
  // receive the same does-not-exist notification.
  auto watcher2 = StartWildcardCapableWatch("wc1");
  EXPECT_TRUE(watcher2->WaitForDoesNotExist(absl::Seconds(1)));
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsWildcardCapableResourceType::Get()->type_url(),
               /*version_info=*/"2", /*response_nonce=*/"B",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"wc1"});
  // Server sends the resource again.
  stream->SendMessageToClient(
      ResponseBuilder(XdsWildcardCapableResourceType::Get()->type_url())
          .set_version_info("3")
          .set_nonce("C")
          .AddWildcardCapableResource(XdsWildcardCapableResource("wc1", 7))
          .Serialize());
  // XdsClient should have delivered the response to the watchers.
  resource = watcher->WaitForNextResource();
  ASSERT_TRUE(resource.has_value());
  EXPECT_EQ(resource->name, "wc1");
  EXPECT_EQ(resource->value, 7);
  resource = watcher2->WaitForNextResource();
  ASSERT_TRUE(resource.has_value());
  EXPECT_EQ(resource->name, "wc1");
  EXPECT_EQ(resource->value, 7);
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsWildcardCapableResourceType::Get()->type_url(),
               /*version_info=*/"3", /*response_nonce=*/"C",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"wc1"});
  // Cancel watch.
  CancelWildcardCapableWatch(watcher.get(), "wc1");
  CancelWildcardCapableWatch(watcher2.get(), "wc1");
  EXPECT_TRUE(stream->Orphaned());
}

// This tests that when we ignore resource deletions from the server
// when configured to do so.
TEST_F(XdsClientTest, ResourceDeletionIgnoredWhenConfigured) {
  InitXdsClient(FakeXdsBootstrap::Builder().set_ignore_resource_deletion(true));
  // Start a watch for "wc1".
  auto watcher = StartWildcardCapableWatch("wc1");
  // Watcher should initially not see any resource reported.
  EXPECT_FALSE(watcher->HasEvent());
  // XdsClient should have created an ADS stream.
  auto stream = WaitForAdsStream();
  ASSERT_TRUE(stream != nullptr);
  // XdsClient should have sent a subscription request on the ADS stream.
  auto request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsWildcardCapableResourceType::Get()->type_url(),
               /*version_info=*/"", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"wc1"});
  CheckRequestNode(*request);  // Should be present on the first request.
  // Server sends a response.
  stream->SendMessageToClient(
      ResponseBuilder(XdsWildcardCapableResourceType::Get()->type_url())
          .set_version_info("1")
          .set_nonce("A")
          .AddWildcardCapableResource(XdsWildcardCapableResource("wc1", 6))
          .Serialize());
  // XdsClient should have delivered the response to the watcher.
  auto resource = watcher->WaitForNextResource();
  ASSERT_TRUE(resource.has_value());
  EXPECT_EQ(resource->name, "wc1");
  EXPECT_EQ(resource->value, 6);
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsWildcardCapableResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"wc1"});
  // Server now sends a response without the resource, thus indicating
  // it's been deleted.
  stream->SendMessageToClient(
      ResponseBuilder(XdsWildcardCapableResourceType::Get()->type_url())
          .set_version_info("2")
          .set_nonce("B")
          .Serialize());
  // Watcher should not see any update, since we should have ignored the
  // deletion.
  EXPECT_TRUE(watcher->ExpectNoEvent(absl::Seconds(1)));
  // Start a new watcher for the same resource.  It should immediately
  // receive the cached resource.
  auto watcher2 = StartWildcardCapableWatch("wc1");
  resource = watcher2->WaitForNextResource();
  ASSERT_TRUE(resource.has_value());
  EXPECT_EQ(resource->name, "wc1");
  EXPECT_EQ(resource->value, 6);
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsWildcardCapableResourceType::Get()->type_url(),
               /*version_info=*/"2", /*response_nonce=*/"B",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"wc1"});
  // Server sends a new value for the resource.
  stream->SendMessageToClient(
      ResponseBuilder(XdsWildcardCapableResourceType::Get()->type_url())
          .set_version_info("3")
          .set_nonce("C")
          .AddWildcardCapableResource(XdsWildcardCapableResource("wc1", 7))
          .Serialize());
  // XdsClient should have delivered the response to the watchers.
  resource = watcher->WaitForNextResource();
  ASSERT_TRUE(resource.has_value());
  EXPECT_EQ(resource->name, "wc1");
  EXPECT_EQ(resource->value, 7);
  resource = watcher2->WaitForNextResource();
  ASSERT_TRUE(resource.has_value());
  EXPECT_EQ(resource->name, "wc1");
  EXPECT_EQ(resource->value, 7);
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsWildcardCapableResourceType::Get()->type_url(),
               /*version_info=*/"3", /*response_nonce=*/"C",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"wc1"});
  // Cancel watch.
  CancelWildcardCapableWatch(watcher.get(), "wc1");
  CancelWildcardCapableWatch(watcher2.get(), "wc1");
  EXPECT_TRUE(stream->Orphaned());
}

TEST_F(XdsClientTest, StreamClosedByServer) {
  InitXdsClient();
  // Start a watch for "foo1".
  auto watcher = StartFooWatch("foo1");
  // Watcher should initially not see any resource reported.
  EXPECT_FALSE(watcher->HasEvent());
  // XdsClient should have created an ADS stream.
  auto stream = WaitForAdsStream();
  ASSERT_TRUE(stream != nullptr);
  // XdsClient should have sent a subscription request on the ADS stream.
  auto request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  CheckRequestNode(*request);  // Should be present on the first request.
  // Server sends a response.
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("1")
          .set_nonce("A")
          .AddFooResource(XdsFooResource("foo1", 6))
          .Serialize());
  // XdsClient should have delivered the response to the watcher.
  auto resource = watcher->WaitForNextResource();
  ASSERT_TRUE(resource.has_value());
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  // Now server closes the stream.
  stream->MaybeSendStatusToClient(absl::OkStatus());
  // XdsClient should NOT report error to watcher, because we saw a
  // response on the stream before it failed.
  // Stream should be orphaned.
  EXPECT_TRUE(stream->Orphaned());
  // XdsClient should create a new stream.
  stream = WaitForAdsStream();
  ASSERT_TRUE(stream != nullptr);
  // XdsClient sends a subscription request.
  // Note that the version persists from the previous stream, but the
  // nonce does not.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  CheckRequestNode(*request);  // Should be present on the first request.
  // Before the server resends the resource, start a new watcher for the
  // same resource.  This watcher should immediately receive the cached
  // resource.
  auto watcher2 = StartFooWatch("foo1");
  resource = watcher2->WaitForNextResource();
  ASSERT_TRUE(resource.has_value());
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  // Server now sends the requested resource.
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("1")
          .set_nonce("B")
          .AddFooResource(XdsFooResource("foo1", 6))
          .Serialize());
  // Watcher does NOT get an update, since the resource has not changed.
  EXPECT_FALSE(watcher->WaitForNextResource());
  // XdsClient sends an ACK.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"B",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  // Cancel watcher.
  CancelFooWatch(watcher.get(), "foo1");
  CancelFooWatch(watcher2.get(), "foo1");
  EXPECT_TRUE(stream->Orphaned());
}

TEST_F(XdsClientTest, StreamClosedByServerWithoutSeeingResponse) {
  InitXdsClient();
  // Start a watch for "foo1".
  auto watcher = StartFooWatch("foo1");
  // Watcher should initially not see any resource reported.
  EXPECT_FALSE(watcher->HasEvent());
  // XdsClient should have created an ADS stream.
  auto stream = WaitForAdsStream();
  ASSERT_TRUE(stream != nullptr);
  // XdsClient should have sent a subscription request on the ADS stream.
  auto request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  CheckRequestNode(*request);  // Should be present on the first request.
  // Server closes the stream without sending a response.
  stream->MaybeSendStatusToClient(absl::UnavailableError("ugh"));
  // XdsClient should report an error to the watcher.
  auto error = watcher->WaitForNextError();
  ASSERT_TRUE(error.has_value());
  EXPECT_EQ(error->code(), absl::StatusCode::kUnavailable);
  EXPECT_EQ(error->message(),
            "xDS channel for server default_xds_server: xDS call failed "
            "with no responses received; status: UNAVAILABLE: ugh "
            "(node ID:xds_client_test)")
      << *error;
  // XdsClient should create a new stream.
  stream = WaitForAdsStream();
  ASSERT_TRUE(stream != nullptr);
  // XdsClient sends a subscription request.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  CheckRequestNode(*request);  // Should be present on the first request.
  // Server now sends the requested resource.
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("1")
          .set_nonce("A")
          .AddFooResource(XdsFooResource("foo1", 6))
          .Serialize());
  // Watcher gets the resource.
  auto resource = watcher->WaitForNextResource();
  ASSERT_TRUE(resource.has_value());
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  // XdsClient sends an ACK.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  // Cancel watcher.
  CancelFooWatch(watcher.get(), "foo1");
  EXPECT_TRUE(stream->Orphaned());
}

TEST_F(XdsClientTest, ConnectionFails) {
  // Lower resources-does-not-exist timeout, to make sure that we're not
  // triggering that here.
  InitXdsClient(FakeXdsBootstrap::Builder(), Duration::Seconds(3));
  // Tell transport to let us manually trigger completion of the
  // send_message ops to XdsClient.
  transport_factory_->SetAutoCompleteMessagesFromClient(false);
  // Start a watch for "foo1".
  auto watcher = StartFooWatch("foo1");
  // Watcher should initially not see any resource reported.
  EXPECT_FALSE(watcher->HasEvent());
  // XdsClient should have created an ADS stream.
  auto stream = WaitForAdsStream();
  ASSERT_TRUE(stream != nullptr);
  // XdsClient should have sent a subscription request on the ADS stream.
  auto request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  CheckRequestNode(*request);  // Should be present on the first request.
  // Transport reports connection failure.
  TriggerConnectionFailure(xds_client_->bootstrap().server(),
                           absl::UnavailableError("connection failed"));
  // XdsClient should report an error to the watcher.
  auto error = watcher->WaitForNextError();
  ASSERT_TRUE(error.has_value());
  EXPECT_EQ(error->code(), absl::StatusCode::kUnavailable);
  EXPECT_EQ(error->message(),
            "xDS channel for server default_xds_server: "
            "connection failed (node ID:xds_client_test)")
      << *error;
  // We should not see a resource-does-not-exist event, because the
  // timer should not be running while the channel is disconnected.
  EXPECT_TRUE(watcher->ExpectNoEvent(absl::Seconds(4)));
  // Start a new watch.  This watcher should be given the same error,
  // since we have not yet recovered.
  auto watcher2 = StartFooWatch("foo1");
  error = watcher2->WaitForNextError();
  ASSERT_TRUE(error.has_value());
  EXPECT_EQ(error->code(), absl::StatusCode::kUnavailable);
  EXPECT_EQ(error->message(),
            "xDS channel for server default_xds_server: "
            "connection failed (node ID:xds_client_test)")
      << *error;
  // Second watcher should not see resource-does-not-exist either.
  EXPECT_FALSE(watcher2->HasEvent());
  // The ADS stream uses wait_for_ready inside the XdsTransport interface,
  // so when the channel reconnects, the already-started stream will proceed.
  stream->CompleteSendMessageFromClient();
  // Server sends a response.
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("1")
          .set_nonce("A")
          .AddFooResource(XdsFooResource("foo1", 6))
          .Serialize());
  // XdsClient should have delivered the response to the watchers.
  auto resource = watcher->WaitForNextResource();
  ASSERT_TRUE(resource.has_value());
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  resource = watcher2->WaitForNextResource();
  ASSERT_TRUE(resource.has_value());
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  stream->CompleteSendMessageFromClient();
  // Cancel watches.
  CancelFooWatch(watcher.get(), "foo1");
  CancelFooWatch(watcher2.get(), "foo1");
  EXPECT_TRUE(stream->Orphaned());
}

TEST_F(XdsClientTest, ResourceDoesNotExistUponTimeout) {
  InitXdsClient(FakeXdsBootstrap::Builder(), Duration::Seconds(1));
  // Start a watch for "foo1".
  auto watcher = StartFooWatch("foo1");
  // Watcher should initially not see any resource reported.
  EXPECT_FALSE(watcher->HasEvent());
  // XdsClient should have created an ADS stream.
  auto stream = WaitForAdsStream();
  ASSERT_TRUE(stream != nullptr);
  // XdsClient should have sent a subscription request on the ADS stream.
  auto request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  CheckRequestNode(*request);  // Should be present on the first request.
  // Do not send a response, but wait for the resource to be reported as
  // not existing.
  EXPECT_TRUE(watcher->WaitForDoesNotExist(absl::Seconds(5)));
  // Start a new watcher for the same resource.  It should immediately
  // receive the same does-not-exist notification.
  auto watcher2 = StartFooWatch("foo1");
  EXPECT_TRUE(watcher2->WaitForDoesNotExist(absl::Seconds(1)));
  // Now server sends a response.
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("1")
          .set_nonce("A")
          .AddFooResource(XdsFooResource("foo1", 6))
          .Serialize());
  // XdsClient should have delivered the response to the watchers.
  auto resource = watcher->WaitForNextResource();
  ASSERT_TRUE(resource.has_value());
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  resource = watcher2->WaitForNextResource();
  ASSERT_TRUE(resource.has_value());
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  // Cancel watch.
  CancelFooWatch(watcher.get(), "foo1");
  CancelFooWatch(watcher2.get(), "foo1");
  EXPECT_TRUE(stream->Orphaned());
}

TEST_F(XdsClientTest, ResourceDoesNotExistAfterStreamRestart) {
  // Lower resources-does-not-exist timeout so test finishes faster.
  InitXdsClient(FakeXdsBootstrap::Builder(), Duration::Seconds(3));
  // Start a watch for "foo1".
  auto watcher = StartFooWatch("foo1");
  // Watcher should initially not see any resource reported.
  EXPECT_FALSE(watcher->HasEvent());
  // XdsClient should have created an ADS stream.
  auto stream = WaitForAdsStream();
  ASSERT_TRUE(stream != nullptr);
  // XdsClient should have sent a subscription request on the ADS stream.
  auto request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  CheckRequestNode(*request);  // Should be present on the first request.
  // Stream fails.
  stream->MaybeSendStatusToClient(absl::UnavailableError("ugh"));
  // XdsClient should report error to watcher.
  auto error = watcher->WaitForNextError();
  ASSERT_TRUE(error.has_value());
  EXPECT_EQ(error->code(), absl::StatusCode::kUnavailable);
  EXPECT_EQ(error->message(),
            "xDS channel for server default_xds_server: xDS call failed "
            "with no responses received; status: UNAVAILABLE: ugh "
            "(node ID:xds_client_test)")
      << *error;
  // XdsClient should create a new stream.
  stream = WaitForAdsStream();
  ASSERT_TRUE(stream != nullptr);
  // XdsClient sends a subscription request.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  CheckRequestNode(*request);  // Should be present on the first request.
  // Server does NOT send a response immediately.
  // Client should receive a resource does-not-exist.
  ASSERT_TRUE(watcher->WaitForDoesNotExist(absl::Seconds(4)));
  // Server now sends the requested resource.
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("1")
          .set_nonce("A")
          .AddFooResource(XdsFooResource("foo1", 6))
          .Serialize());
  // The resource is delivered to the watcher.
  auto resource = watcher->WaitForNextResource();
  ASSERT_TRUE(resource.has_value());
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  // XdsClient sends an ACK.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  // Cancel watcher.
  CancelFooWatch(watcher.get(), "foo1");
  EXPECT_TRUE(stream->Orphaned());
}

TEST_F(XdsClientTest, DoesNotExistTimerNotStartedUntilSendCompletes) {
  // Lower resources-does-not-exist timeout, to make sure that we're not
  // triggering that here.
  InitXdsClient(FakeXdsBootstrap::Builder(), Duration::Seconds(3));
  // Tell transport to let us manually trigger completion of the
  // send_message ops to XdsClient.
  transport_factory_->SetAutoCompleteMessagesFromClient(false);
  // Start a watch for "foo1".
  auto watcher = StartFooWatch("foo1");
  // Watcher should initially not see any resource reported.
  EXPECT_FALSE(watcher->HasEvent());
  // XdsClient should have created an ADS stream.
  auto stream = WaitForAdsStream();
  ASSERT_TRUE(stream != nullptr);
  // XdsClient should have sent a subscription request on the ADS stream.
  auto request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  CheckRequestNode(*request);  // Should be present on the first request.
  // Server does NOT send a response.
  // We should not see a resource-does-not-exist event, because the
  // timer should not be running while the channel is disconnected.
  EXPECT_TRUE(watcher->ExpectNoEvent(absl::Seconds(4)));
  // The ADS stream uses wait_for_ready inside the XdsTransport interface,
  // so when the channel connects, the already-started stream will proceed.
  stream->CompleteSendMessageFromClient();
  // Server does NOT send a response.
  // Watcher should see a does-not-exist event.
  EXPECT_TRUE(watcher->WaitForDoesNotExist(absl::Seconds(4)));
  // Now server sends a response.
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("1")
          .set_nonce("A")
          .AddFooResource(XdsFooResource("foo1", 6))
          .Serialize());
  // XdsClient should have delivered the response to the watcher.
  auto resource = watcher->WaitForNextResource();
  ASSERT_TRUE(resource.has_value());
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  stream->CompleteSendMessageFromClient();
  // Cancel watch.
  CancelFooWatch(watcher.get(), "foo1");
  EXPECT_TRUE(stream->Orphaned());
}

// In https://github.com/grpc/grpc/issues/29583, we ran into a case
// where we wound up starting a timer after we had already received the
// resource, thus incorrectly reporting the resource as not existing.
// This happened when unsubscribing and then resubscribing to the same
// resource a send_message op was already in flight and then receiving an
// update containing that resource.
TEST_F(XdsClientTest,
       ResourceDoesNotExistUnsubscribeAndResubscribeWhileSendMessagePending) {
  InitXdsClient(FakeXdsBootstrap::Builder(), Duration::Seconds(1));
  // Tell transport to let us manually trigger completion of the
  // send_message ops to XdsClient.
  transport_factory_->SetAutoCompleteMessagesFromClient(false);
  // Start a watch for "foo1".
  auto watcher = StartFooWatch("foo1");
  // Watcher should initially not see any resource reported.
  EXPECT_FALSE(watcher->HasEvent());
  // XdsClient should have created an ADS stream.
  auto stream = WaitForAdsStream();
  ASSERT_TRUE(stream != nullptr);
  // XdsClient should have sent a subscription request on the ADS stream.
  auto request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  CheckRequestNode(*request);  // Should be present on the first request.
  stream->CompleteSendMessageFromClient();
  // Server sends a response.
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("1")
          .set_nonce("A")
          .AddFooResource(XdsFooResource("foo1", 6))
          .Serialize());
  // XdsClient should have delivered the response to the watchers.
  auto resource = watcher->WaitForNextResource();
  ASSERT_TRUE(resource.has_value());
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  stream->CompleteSendMessageFromClient();
  // Start a watch for a second resource.
  auto watcher2 = StartFooWatch("foo2");
  // Watcher should initially not see any resource reported.
  EXPECT_FALSE(watcher2->HasEvent());
  // XdsClient sends a request to subscribe to the new resource.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1", "foo2"});
  // NOTE: We do NOT yet tell the XdsClient that the send_message op is
  // complete.
  // Unsubscribe from foo1 and then re-subscribe to it.
  CancelFooWatch(watcher.get(), "foo1");
  watcher = StartFooWatch("foo1");
  // Now send a response from the server containing both foo1 and foo2.
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("1")
          .set_nonce("B")
          .AddFooResource(XdsFooResource("foo1", 6))
          .AddFooResource(XdsFooResource("foo2", 7))
          .Serialize());
  // The watcher for foo1 will receive an update even if the resource
  // has not changed, since the previous value was removed from the
  // cache when we unsubscribed.
  resource = watcher->WaitForNextResource();
  ASSERT_TRUE(resource.has_value());
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  // For foo2, the watcher should receive notification for the new resource.
  resource = watcher2->WaitForNextResource();
  ASSERT_TRUE(resource.has_value());
  EXPECT_EQ(resource->name, "foo2");
  EXPECT_EQ(resource->value, 7);
  // Now we finally tell XdsClient that its previous send_message op is
  // complete.
  stream->CompleteSendMessageFromClient();
  // XdsClient should send an ACK with the updated subscription list
  // (which happens to be identical to the old list), and it should not
  // restart the does-not-exist timer.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"B",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1", "foo2"});
  stream->CompleteSendMessageFromClient();
  // Make sure the watcher for foo1 does not see a does-not-exist event.
  EXPECT_TRUE(watcher->ExpectNoEvent(absl::Seconds(5)));
  // Cancel watches.
  CancelFooWatch(watcher.get(), "foo1", /*delay_unsubscription=*/true);
  CancelFooWatch(watcher2.get(), "foo2");
  EXPECT_TRUE(stream->Orphaned());
}

TEST_F(XdsClientTest, DoNotSendDoesNotExistForCachedResource) {
  // Lower resources-does-not-exist timeout, to make sure that we're not
  // triggering that here.
  InitXdsClient(FakeXdsBootstrap::Builder(), Duration::Seconds(3));
  // Start a watch for "foo1".
  auto watcher = StartFooWatch("foo1");
  // Watcher should initially not see any resource reported.
  EXPECT_FALSE(watcher->HasEvent());
  // XdsClient should have created an ADS stream.
  auto stream = WaitForAdsStream();
  ASSERT_TRUE(stream != nullptr);
  // XdsClient should have sent a subscription request on the ADS stream.
  auto request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  CheckRequestNode(*request);  // Should be present on the first request.
  // Server sends a response.
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("1")
          .set_nonce("A")
          .AddFooResource(XdsFooResource("foo1", 6))
          .Serialize());
  // XdsClient should have delivered the response to the watcher.
  auto resource = watcher->WaitForNextResource();
  ASSERT_TRUE(resource.has_value());
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  // Stream fails because of transport disconnection.
  stream->MaybeSendStatusToClient(absl::UnavailableError("connection failed"));
  // XdsClient should NOT report error to watcher, because we saw a
  // response on the stream before it failed.
  // XdsClient creates a new stream.
  stream = WaitForAdsStream();
  ASSERT_TRUE(stream != nullptr);
  // XdsClient should have sent a subscription request on the ADS stream.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  CheckRequestNode(*request);  // Should be present on the first request.
  // Server does NOT send a response.
  // We should not see a resource-does-not-exist event, because the
  // resource was already cached, so the server can optimize by not
  // resending it.
  EXPECT_TRUE(watcher->ExpectNoEvent(absl::Seconds(4)));
  // Now server sends a response.
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("1")
          .set_nonce("A")
          .AddFooResource(XdsFooResource("foo1", 6))
          .Serialize());
  // Watcher will not see any update, since the resource is unchanged.
  EXPECT_TRUE(watcher->ExpectNoEvent(absl::Seconds(1)));
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  // Cancel watch.
  CancelFooWatch(watcher.get(), "foo1");
  EXPECT_TRUE(stream->Orphaned());
}

TEST_F(XdsClientTest, ResourceWrappedInResourceMessage) {
  InitXdsClient();
  // Start a watch for "foo1".
  auto watcher = StartFooWatch("foo1");
  // Watcher should initially not see any resource reported.
  EXPECT_FALSE(watcher->HasEvent());
  // XdsClient should have created an ADS stream.
  auto stream = WaitForAdsStream();
  ASSERT_TRUE(stream != nullptr);
  // XdsClient should have sent a subscription request on the ADS stream.
  auto request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  CheckRequestNode(*request);  // Should be present on the first request.
  // Send a response with the resource wrapped in a Resource message.
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("1")
          .set_nonce("A")
          .AddFooResource(XdsFooResource("foo1", 6),
                          /*in_resource_wrapper=*/true)
          .Serialize());
  // XdsClient should have delivered the response to the watcher.
  auto resource = watcher->WaitForNextResource();
  ASSERT_TRUE(resource.has_value());
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  // Cancel watch.
  CancelFooWatch(watcher.get(), "foo1");
  EXPECT_TRUE(stream->Orphaned());
}

TEST_F(XdsClientTest, MultipleResourceTypes) {
  InitXdsClient();
  // Start a watch for "foo1".
  auto watcher = StartFooWatch("foo1");
  // Watcher should initially not see any resource reported.
  EXPECT_FALSE(watcher->HasEvent());
  // XdsClient should have created an ADS stream.
  auto stream = WaitForAdsStream();
  ASSERT_TRUE(stream != nullptr);
  // XdsClient should have sent a subscription request on the ADS stream.
  auto request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  CheckRequestNode(*request);  // Should be present on the first request.
  // Send a response.
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("1")
          .set_nonce("A")
          .AddFooResource(XdsFooResource("foo1", 6))
          .Serialize());
  // XdsClient should have delivered the response to the watcher.
  auto resource = watcher->WaitForNextResource();
  ASSERT_TRUE(resource.has_value());
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  // Start a watch for "bar1".
  auto watcher2 = StartBarWatch("bar1");
  // XdsClient should have sent a subscription request on the ADS stream.
  // Note that version and nonce here do NOT use the values for Foo,
  // since each resource type has its own state.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsBarResourceType::Get()->type_url(),
               /*version_info=*/"", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"bar1"});
  // Send a response.
  stream->SendMessageToClient(
      ResponseBuilder(XdsBarResourceType::Get()->type_url())
          .set_version_info("2")
          .set_nonce("B")
          .AddBarResource(XdsBarResource("bar1", "whee"))
          .Serialize());
  // XdsClient should have delivered the response to the watcher.
  auto resource2 = watcher2->WaitForNextResource();
  ASSERT_TRUE(resource2.has_value());
  EXPECT_EQ(resource2->name, "bar1");
  EXPECT_EQ(resource2->value, "whee");
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsBarResourceType::Get()->type_url(),
               /*version_info=*/"2", /*response_nonce=*/"B",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"bar1"});
  // Cancel watch for "foo1".
  CancelFooWatch(watcher.get(), "foo1");
  // XdsClient should send an unsubscription request.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(), /*resource_names=*/{});
  // Now cancel watch for "bar1".
  CancelBarWatch(watcher2.get(), "bar1");
  EXPECT_TRUE(stream->Orphaned());
}

TEST_F(XdsClientTest, Federation) {
  constexpr char kAuthority[] = "xds.example.com";
  const std::string kXdstpResourceName = absl::StrCat(
      "xdstp://", kAuthority, "/", XdsFooResource::TypeUrl(), "/foo2");
  FakeXdsBootstrap::FakeXdsServer authority_server;
  authority_server.set_server_uri("other_xds_server");
  FakeXdsBootstrap::FakeAuthority authority;
  authority.set_server(authority_server);
  InitXdsClient(
      FakeXdsBootstrap::Builder().AddAuthority(kAuthority, authority));
  // Start a watch for "foo1".
  auto watcher = StartFooWatch("foo1");
  // Watcher should initially not see any resource reported.
  EXPECT_FALSE(watcher->HasEvent());
  // XdsClient should have created an ADS stream to the top-level xDS server.
  auto stream = WaitForAdsStream(xds_client_->bootstrap().server());
  ASSERT_TRUE(stream != nullptr);
  // XdsClient should have sent a subscription request on the ADS stream.
  auto request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  CheckRequestNode(*request);  // Should be present on the first request.
  // Send a response.
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("1")
          .set_nonce("A")
          .AddFooResource(XdsFooResource("foo1", 6))
          .Serialize());
  // XdsClient should have delivered the response to the watcher.
  auto resource = watcher->WaitForNextResource();
  ASSERT_TRUE(resource.has_value());
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  // Start a watch for the xdstp resource name.
  auto watcher2 = StartFooWatch(kXdstpResourceName);
  // Watcher should initially not see any resource reported.
  EXPECT_FALSE(watcher2->HasEvent());
  // XdsClient will create a new stream to the server for this authority.
  auto stream2 = WaitForAdsStream(authority_server);
  ASSERT_TRUE(stream2 != nullptr);
  // XdsClient should have sent a subscription request on the ADS stream.
  // Note that version and nonce here do NOT use the values for Foo,
  // since each authority has its own state.
  request = WaitForRequest(stream2.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{kXdstpResourceName});
  CheckRequestNode(*request);  // Should be present on the first request.
  // Send a response.
  stream2->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("2")
          .set_nonce("B")
          .AddFooResource(XdsFooResource(kXdstpResourceName, 3))
          .Serialize());
  // XdsClient should have delivered the response to the watcher.
  resource = watcher2->WaitForNextResource();
  ASSERT_TRUE(resource.has_value());
  EXPECT_EQ(resource->name, kXdstpResourceName);
  EXPECT_EQ(resource->value, 3);
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream2.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"2", /*response_nonce=*/"B",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{kXdstpResourceName});
  // Cancel watch for "foo1".
  CancelFooWatch(watcher.get(), "foo1");
  EXPECT_TRUE(stream->Orphaned());
  // Now cancel watch for xdstp resource name.
  CancelFooWatch(watcher2.get(), kXdstpResourceName);
  EXPECT_TRUE(stream2->Orphaned());
}

TEST_F(XdsClientTest, FederationAuthorityDefaultsToTopLevelXdsServer) {
  constexpr char kAuthority[] = "xds.example.com";
  const std::string kXdstpResourceName = absl::StrCat(
      "xdstp://", kAuthority, "/", XdsFooResource::TypeUrl(), "/foo2");
  // Authority does not specify any xDS servers, so XdsClient will use
  // the top-level xDS server in the bootstrap config for this authority.
  InitXdsClient(FakeXdsBootstrap::Builder().AddAuthority(
      kAuthority, FakeXdsBootstrap::FakeAuthority()));
  // Start a watch for "foo1".
  auto watcher = StartFooWatch("foo1");
  // Watcher should initially not see any resource reported.
  EXPECT_FALSE(watcher->HasEvent());
  // XdsClient should have created an ADS stream to the top-level xDS server.
  auto stream = WaitForAdsStream(xds_client_->bootstrap().server());
  ASSERT_TRUE(stream != nullptr);
  // XdsClient should have sent a subscription request on the ADS stream.
  auto request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  CheckRequestNode(*request);  // Should be present on the first request.
  // Send a response.
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("1")
          .set_nonce("A")
          .AddFooResource(XdsFooResource("foo1", 6))
          .Serialize());
  // XdsClient should have delivered the response to the watcher.
  auto resource = watcher->WaitForNextResource();
  ASSERT_TRUE(resource.has_value());
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  // Start a watch for the xdstp resource name.
  auto watcher2 = StartFooWatch(kXdstpResourceName);
  // Watcher should initially not see any resource reported.
  EXPECT_FALSE(watcher2->HasEvent());
  // XdsClient will send a subscription request on the ADS stream that
  // includes both resources, since both are being obtained from the
  // same server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1", kXdstpResourceName});
  // Send a response.
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("2")
          .set_nonce("B")
          .AddFooResource(XdsFooResource(kXdstpResourceName, 3))
          .Serialize());
  // XdsClient should have delivered the response to the watcher.
  resource = watcher2->WaitForNextResource();
  ASSERT_TRUE(resource.has_value());
  EXPECT_EQ(resource->name, kXdstpResourceName);
  EXPECT_EQ(resource->value, 3);
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"2", /*response_nonce=*/"B",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1", kXdstpResourceName});
  // Cancel watch for "foo1".
  CancelFooWatch(watcher.get(), "foo1");
  // XdsClient should send an unsubscription request.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"2", /*response_nonce=*/"B",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{kXdstpResourceName});
  // Now cancel watch for xdstp resource name.
  CancelFooWatch(watcher2.get(), kXdstpResourceName);
  EXPECT_TRUE(stream->Orphaned());
}

TEST_F(XdsClientTest, FederationWithUnknownAuthority) {
  constexpr char kAuthority[] = "xds.example.com";
  const std::string kXdstpResourceName = absl::StrCat(
      "xdstp://", kAuthority, "/", XdsFooResource::TypeUrl(), "/foo2");
  // Note: Not adding authority to bootstrap config.
  InitXdsClient();
  // Start a watch for the xdstp resource name.
  auto watcher = StartFooWatch(kXdstpResourceName);
  // Watcher should immediately get an error about the unknown authority.
  auto error = watcher->WaitForNextError();
  ASSERT_TRUE(error.has_value());
  EXPECT_EQ(error->code(), absl::StatusCode::kUnavailable);
  EXPECT_EQ(error->message(),
            "authority \"xds.example.com\" not present in bootstrap config")
      << *error;
}

TEST_F(XdsClientTest, FederationWithUnparseableXdstpResourceName) {
  // Note: Not adding authority to bootstrap config.
  InitXdsClient();
  // Start a watch for the xdstp resource name.
  auto watcher = StartFooWatch("xdstp://x");
  // Watcher should immediately get an error about the unknown authority.
  auto error = watcher->WaitForNextError();
  ASSERT_TRUE(error.has_value());
  EXPECT_EQ(error->code(), absl::StatusCode::kUnavailable);
  EXPECT_EQ(error->message(), "Unable to parse resource name xdstp://x")
      << *error;
}

// TODO(roth,apolcyn): remove this test when the
// GRPC_EXPERIMENTAL_XDS_FEDERATION env var is removed.
TEST_F(XdsClientTest, FederationDisabledWithNewStyleName) {
  testing::ScopedEnvVar env_var("GRPC_EXPERIMENTAL_XDS_FEDERATION", "false");
  // We will use this xdstp name, whose authority is not present in
  // the bootstrap config.  But since federation is not enabled, we
  // will treat this as an opaque old-style name, so we'll send it to
  // the default server.
  constexpr char kXdstpResourceName[] =
      "xdstp://xds.example.com/test.v3.foo/foo1";
  InitXdsClient();
  // Start a watch for the xdstp name.
  auto watcher = StartFooWatch(kXdstpResourceName);
  // Watcher should initially not see any resource reported.
  EXPECT_FALSE(watcher->HasEvent());
  // XdsClient should have created an ADS stream.
  auto stream = WaitForAdsStream();
  ASSERT_TRUE(stream != nullptr);
  // XdsClient should have sent a subscription request on the ADS stream.
  auto request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{kXdstpResourceName});
  CheckRequestNode(*request);  // Should be present on the first request.
  // Send a response.
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("1")
          .set_nonce("A")
          .AddFooResource(XdsFooResource(kXdstpResourceName, 6))
          .Serialize());
  // XdsClient should have delivered the response to the watcher.
  auto resource = watcher->WaitForNextResource();
  ASSERT_TRUE(resource.has_value());
  EXPECT_EQ(resource->name, kXdstpResourceName);
  EXPECT_EQ(resource->value, 6);
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{kXdstpResourceName});
  // Cancel watch.
  CancelFooWatch(watcher.get(), kXdstpResourceName);
  EXPECT_TRUE(stream->Orphaned());
}

TEST_F(XdsClientTest, FederationChannelFailureReportedToWatchers) {
  constexpr char kAuthority[] = "xds.example.com";
  const std::string kXdstpResourceName = absl::StrCat(
      "xdstp://", kAuthority, "/", XdsFooResource::TypeUrl(), "/foo2");
  FakeXdsBootstrap::FakeXdsServer authority_server;
  authority_server.set_server_uri("other_xds_server");
  FakeXdsBootstrap::FakeAuthority authority;
  authority.set_server(authority_server);
  InitXdsClient(
      FakeXdsBootstrap::Builder().AddAuthority(kAuthority, authority));
  // Start a watch for "foo1".
  auto watcher = StartFooWatch("foo1");
  // Watcher should initially not see any resource reported.
  EXPECT_FALSE(watcher->HasEvent());
  // XdsClient should have created an ADS stream to the top-level xDS server.
  auto stream = WaitForAdsStream(xds_client_->bootstrap().server());
  ASSERT_TRUE(stream != nullptr);
  // XdsClient should have sent a subscription request on the ADS stream.
  auto request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  CheckRequestNode(*request);  // Should be present on the first request.
  // Send a response.
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("1")
          .set_nonce("A")
          .AddFooResource(XdsFooResource("foo1", 6))
          .Serialize());
  // XdsClient should have delivered the response to the watcher.
  auto resource = watcher->WaitForNextResource();
  ASSERT_TRUE(resource.has_value());
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  // Start a watch for the xdstp resource name.
  auto watcher2 = StartFooWatch(kXdstpResourceName);
  // Watcher should initially not see any resource reported.
  EXPECT_FALSE(watcher2->HasEvent());
  // XdsClient will create a new stream to the server for this authority.
  auto stream2 = WaitForAdsStream(authority_server);
  ASSERT_TRUE(stream2 != nullptr);
  // XdsClient should have sent a subscription request on the ADS stream.
  // Note that version and nonce here do NOT use the values for Foo,
  // since each authority has its own state.
  request = WaitForRequest(stream2.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{kXdstpResourceName});
  CheckRequestNode(*request);  // Should be present on the first request.
  // Send a response.
  stream2->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("2")
          .set_nonce("B")
          .AddFooResource(XdsFooResource(kXdstpResourceName, 3))
          .Serialize());
  // XdsClient should have delivered the response to the watcher.
  resource = watcher2->WaitForNextResource();
  ASSERT_TRUE(resource.has_value());
  EXPECT_EQ(resource->name, kXdstpResourceName);
  EXPECT_EQ(resource->value, 3);
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream2.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"2", /*response_nonce=*/"B",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{kXdstpResourceName});
  // Now cause a channel failure on the stream to the authority's xDS server.
  TriggerConnectionFailure(authority_server,
                           absl::UnavailableError("connection failed"));
  // The watcher for the xdstp resource name should see the error.
  auto error = watcher2->WaitForNextError();
  ASSERT_TRUE(error.has_value());
  EXPECT_EQ(error->code(), absl::StatusCode::kUnavailable);
  EXPECT_EQ(error->message(),
            "xDS channel for server other_xds_server: connection failed "
            "(node ID:xds_client_test)")
      << *error;
  // The watcher for "foo1" should not see any error.
  EXPECT_FALSE(watcher->HasEvent());
  // Cancel watch for "foo1".
  CancelFooWatch(watcher.get(), "foo1");
  EXPECT_TRUE(stream->Orphaned());
  // Now cancel watch for xdstp resource name.
  CancelFooWatch(watcher2.get(), kXdstpResourceName);
  EXPECT_TRUE(stream2->Orphaned());
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
