//
// Copyright 2023 gRPC authors.
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

#ifndef GRPC_TEST_CORE_XDS_XDS_CLIENT_TEST_LIB_H
#define GRPC_TEST_CORE_XDS_XDS_CLIENT_TEST_LIB_H

#include <stdint.h>

#include <algorithm>
#include <deque>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>

#include <google/protobuf/any.pb.h>

#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "absl/types/optional.h"
#include "absl/types/variant.h"
#include "gmock/gmock.h"
#include "google/protobuf/json/json.h"
#include "google/protobuf/struct.pb.h"
#include "google/protobuf/util/json_util.h"
#include "gtest/gtest.h"
#include "upb/reflection/def.h"
#include "xds_transport_fake.h"

#include <grpc/support/json.h>
#include <grpc/support/log.h>
#include <grpcpp/impl/codegen/config_protobuf.h>

#include "src/core/ext/xds/xds_bootstrap.h"
#include "src/core/ext/xds/xds_client.h"
#include "src/core/ext/xds/xds_resource_type.h"
#include "src/core/ext/xds/xds_resource_type_impl.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/json/json_args.h"
#include "src/core/lib/json/json_object_loader.h"
#include "src/core/lib/json/json_reader.h"
#include "src/core/lib/json/json_writer.h"
#include "src/proto/grpc/testing/xds/v3/base.pb.h"
#include "src/proto/grpc/testing/xds/v3/discovery.pb.h"
#include "test/core/util/test_config.h"
#include "test/core/xds/xds_transport_fake.h"

namespace grpc_core {
namespace testing {

using envoy::service::discovery::v3::DiscoveryRequest;
using envoy::service::discovery::v3::DiscoveryResponse;

class XdsClientTestBase : public ::testing::Test {
 protected:
  // A fake bootstrap implementation that allows tests to populate the
  // fields however they want.
  class FakeXdsBootstrap : public XdsBootstrap {
   public:
    class FakeNode : public Node {
     public:
      FakeNode() : id_("xds_client_test") {}
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
      std::string id_;
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
    class ResourceAndReadDelayHandle {
     public:
      ResourceAndReadDelayHandle(
          std::shared_ptr<const ResourceStruct> resource,
          RefCountedPtr<XdsClient::ReadDelayHandle> read_delay_handle)
          : resource_(std::move(resource)),
            read_delay_handle_(std::move(read_delay_handle)) {}

      absl::string_view name() const { return resource_->name; }

      int resource_value() const { return resource_->value; }

     private:
      std::shared_ptr<const ResourceStruct> resource_;
      RefCountedPtr<XdsClient::ReadDelayHandle> read_delay_handle_;
    };

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

      absl::optional<ResourceAndReadDelayHandle> WaitForNextResourceAndHandle(
          absl::Duration timeout = absl::Seconds(1),
          SourceLocation location = SourceLocation()) {
        MutexLock lock(&mu_);
        if (!WaitForEventLocked(timeout)) return absl::nullopt;
        Event& event = queue_.front();
        if (!absl::holds_alternative<ResourceAndReadDelayHandle>(event)) {
          EXPECT_TRUE(false)
              << "got unexpected event "
              << (absl::holds_alternative<absl::Status>(event)
                      ? "error"
                      : "does-not-exist")
              << " at " << location.file() << ":" << location.line();
          return absl::nullopt;
        }
        auto foo = std::move(absl::get<ResourceAndReadDelayHandle>(event));
        queue_.pop_front();
        return foo;
      }

      std::shared_ptr<const ResourceStruct> WaitForNextResource(
          absl::Duration timeout = absl::Seconds(1),
          SourceLocation location = SourceLocation()) {
        auto resource_and_handle =
            WaitForNextResourceAndHandle(timeout, location);
        if (!resource_and_handle.has_value()) {
          return nullptr;
        }
        return std::move(resource_and_handle->first);
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
              << (absl::holds_alternative<ResourceAndReadDelayHandle>(event)
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
              << (absl::holds_alternative<absl::Status>(event) ? "error"
                                                               : "resource")
              << " at " << location.file() << ":" << location.line();
          return false;
        }
        queue_.pop_front();
        return true;
      }

     private:
      struct DoesNotExist {};
      using Event =
          absl::variant<ResourceAndReadDelayHandle, absl::Status, DoesNotExist>;

      void OnResourceChanged(std::shared_ptr<const ResourceStruct> foo,
                             RefCountedPtr<XdsClient::ReadDelayHandle>
                                 read_delay_handle) override {
        MutexLock lock(&mu_);
        queue_.emplace_back(ResourceAndReadDelayHandle(
            std::move(foo), std::move(read_delay_handle)));
        cv_.Signal();
      }

      void OnError(
          absl::Status status,
          RefCountedPtr<XdsClient::ReadDelayHandle> /* read_delay_handle */)
          override {
        MutexLock lock(&mu_);
        queue_.push_back(std::move(status));
        cv_.Signal();
      }

      void OnResourceDoesNotExist(
          RefCountedPtr<XdsClient::ReadDelayHandle> /* read_delay_handle */)
          override {
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
      auto json = JsonParse(serialized_resource);
      XdsResourceType::DecodeResult result;
      if (!json.ok()) {
        result.resource = json.status();
      } else {
        absl::StatusOr<ResourceStruct> foo =
            LoadFromJson<ResourceStruct>(*json);
        if (!foo.ok()) {
          auto it = json->object().find("name");
          if (it != json->object().end()) {
            result.name = it->second.string();
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

    ResponseBuilder& AddInvalidResourceWrapper() {
      auto* res = response_.add_resources();
      res->set_type_url(
          "type.googleapis.com/envoy.service.discovery.v3.Resource");
      res->set_value(std::string("\0", 1));
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
                    absl::string_view response_nonce,
                    const absl::Status& error_detail,
                    const std::set<absl::string_view>& resource_names,
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
      auto metadata_json = JsonParse(metadata_json_str);
      ASSERT_TRUE(metadata_json.ok())
          << metadata_json.status() << " on " << location.file() << ":"
          << location.line();
      Json expected =
          Json::FromObject(xds_client_->bootstrap().node()->metadata());
      EXPECT_EQ(*metadata_json, expected)
          << location.file() << ":" << location.line()
          << ":\nexpected: " << JsonDump(expected)
          << "\nactual: " << JsonDump(*metadata_json);
    }
    EXPECT_EQ(request.node().user_agent_name(), "foo agent")
        << location.file() << ":" << location.line();
    EXPECT_EQ(request.node().user_agent_version(), "foo version")
        << location.file() << ":" << location.line();
  }

  RefCountedPtr<FakeXdsTransportFactory> transport_factory_;
  RefCountedPtr<XdsClient> xds_client_;
};

}  // namespace testing
}  // namespace grpc_core

#endif  // GRPC_TEST_CORE_XDS_XDS_CLIENT_TEST_LIB_H