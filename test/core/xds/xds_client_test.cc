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
// - tests for load-reporting APIs?  (or maybe move those out of XdsClient?)

#include "src/core/xds/xds_client/xds_client.h"

#include <google/protobuf/any.pb.h>
#include <google/protobuf/struct.pb.h>
#include <grpc/grpc.h>
#include <grpc/support/json.h>
#include <grpcpp/impl/codegen/config_protobuf.h>
#include <stdint.h>

#include <algorithm>
#include <deque>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/service/discovery/v3/discovery.pb.h"
#include "envoy/service/status/v3/csds.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/lib/iomgr/timer_manager.h"
#include "src/core/util/debug_location.h"
#include "src/core/util/json/json.h"
#include "src/core/util/json/json_args.h"
#include "src/core/util/json/json_object_loader.h"
#include "src/core/util/json/json_reader.h"
#include "src/core/util/json/json_writer.h"
#include "src/core/util/match.h"
#include "src/core/util/sync.h"
#include "src/core/util/wait_for_single_owner.h"
#include "src/core/xds/xds_client/xds_bootstrap.h"
#include "src/core/xds/xds_client/xds_resource_type_impl.h"
#include "test/core/event_engine/event_engine_test_utils.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.h"
#include "test/core/test_util/scoped_env_var.h"
#include "test/core/test_util/test_config.h"
#include "test/core/xds/xds_client_test_peer.h"
#include "test/core/xds/xds_transport_fake.h"
#include "upb/reflection/def.h"

// IWYU pragma: no_include <google/protobuf/message.h>
// IWYU pragma: no_include <google/protobuf/stubs/status.h>
// IWYU pragma: no_include <google/protobuf/unknown_field_set.h>
// IWYU pragma: no_include <google/protobuf/util/json_util.h>
// IWYU pragma: no_include "google/protobuf/json/json.h"
// IWYU pragma: no_include "google/protobuf/util/json_util.h"

using envoy::admin::v3::ClientResourceStatus;
using envoy::service::discovery::v3::DiscoveryRequest;
using envoy::service::discovery::v3::DiscoveryResponse;
using envoy::service::status::v3::ClientConfig;
using grpc_event_engine::experimental::FuzzingEventEngine;

namespace grpc_core {
namespace testing {
namespace {

constexpr absl::string_view kDefaultXdsServerUrl = "default_xds_server";

constexpr Timestamp kTime0 =
    Timestamp::FromMillisecondsAfterProcessEpoch(10000);
constexpr Timestamp kTime1 =
    Timestamp::FromMillisecondsAfterProcessEpoch(15000);
constexpr Timestamp kTime2 =
    Timestamp::FromMillisecondsAfterProcessEpoch(20000);

class XdsClientTest : public ::testing::Test {
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

    class FakeXdsServerTarget : public XdsServerTarget {
     public:
      explicit FakeXdsServerTarget(std::string server_uri)
          : server_uri_(std::move(server_uri)) {}
      const std::string& server_uri() const override { return server_uri_; }
      std::string Key() const override { return server_uri_; }
      bool Equals(const XdsServerTarget& other) const override {
        const auto& o = DownCast<const FakeXdsServerTarget&>(other);
        return server_uri_ == o.server_uri_;
      }

     private:
      std::string server_uri_;
    };

    class FakeXdsServer : public XdsServer {
     public:
      explicit FakeXdsServer(
          absl::string_view server_uri = kDefaultXdsServerUrl,
          bool fail_on_data_errors = false,
          bool resource_timer_is_transient_failure = false)
          : server_target_(
                std::make_shared<FakeXdsServerTarget>(std::string(server_uri))),
            fail_on_data_errors_(fail_on_data_errors),
            resource_timer_is_transient_failure_(
                resource_timer_is_transient_failure) {}
      bool IgnoreResourceDeletion() const override {
        return !fail_on_data_errors_;
      }
      bool FailOnDataErrors() const override { return fail_on_data_errors_; }
      bool ResourceTimerIsTransientFailure() const override {
        return resource_timer_is_transient_failure_;
      }
      bool Equals(const XdsServer& other) const override {
        const auto& o = static_cast<const FakeXdsServer&>(other);
        return *server_target_ == *o.server_target_ &&
               fail_on_data_errors_ == o.fail_on_data_errors_;
      }
      std::string Key() const override {
        return absl::StrCat(server_target_->server_uri(), "#",
                            fail_on_data_errors_);
      }
      std::shared_ptr<const XdsServerTarget> target() const override {
        return server_target_;
      }

     private:
      std::shared_ptr<FakeXdsServerTarget> server_target_;
      bool fail_on_data_errors_ = false;
      bool resource_timer_is_transient_failure_ = false;
    };

    class FakeAuthority : public Authority {
     public:
      std::vector<const XdsServer*> servers() const override {
        if (server_.has_value()) {
          return {&*server_};
        } else {
          return {};
        };
      }

      void set_server(std::optional<FakeXdsServer> server) {
        server_ = std::move(server);
      }

     private:
      std::optional<FakeXdsServer> server_;
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
      Builder& SetServers(absl::Span<const FakeXdsServer> servers) {
        servers_.assign(servers.begin(), servers.end());
        return *this;
      }
      std::unique_ptr<XdsBootstrap> Build() {
        auto bootstrap = std::make_unique<FakeXdsBootstrap>();
        bootstrap->servers_ = std::move(servers_);
        bootstrap->node_ = std::move(node_);
        bootstrap->authorities_ = std::move(authorities_);
        return bootstrap;
      }

     private:
      std::vector<FakeXdsServer> servers_ = {FakeXdsServer()};
      std::optional<FakeNode> node_;
      std::map<std::string, FakeAuthority> authorities_;
    };

    std::string ToString() const override { return "<fake>"; }

    std::vector<const XdsServer*> servers() const override {
      std::vector<const XdsServer*> result;
      result.reserve(servers_.size());
      for (size_t i = 0; i < servers_.size(); ++i) {
        result.emplace_back(&servers_[i]);
      }
      return result;
    }

    const Node* node() const override {
      return node_.has_value() ? &*node_ : nullptr;
    }
    const Authority* LookupAuthority(const std::string& name) const override {
      auto it = authorities_.find(name);
      if (it == authorities_.end()) return nullptr;
      return &it->second;
    }

   private:
    std::vector<FakeXdsServer> servers_;
    std::optional<FakeNode> node_;
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
      explicit Watcher(std::shared_ptr<FuzzingEventEngine> event_engine)
          : event_engine_(std::move(event_engine)) {}

      ~Watcher() override {
        MutexLock lock(&mu_);
        EXPECT_THAT(queue_, ::testing::IsEmpty())
            << this << " " << queue_[0].ToString();
      }

      bool HasEvent() {
        MutexLock lock(&mu_);
        return !queue_.empty();
      }

      // Returns true if no event is received after draining the fuzzing
      // EE queue.
      bool ExpectNoEvent() {
        event_engine_->TickUntilIdle();
        return !HasEvent();
      }

      struct ResourceAndReadDelayHandle {
        std::shared_ptr<const ResourceStruct> resource;
        RefCountedPtr<XdsClient::ReadDelayHandle> read_delay_handle;
      };
      std::optional<ResourceAndReadDelayHandle> WaitForNextResourceAndHandle(
          SourceLocation location = SourceLocation()) {
        auto event = WaitForNextOnResourceChangedEvent(location);
        if (!event.has_value()) return std::nullopt;
        EXPECT_TRUE(event->resource.ok())
            << "got unexpected error: " << event->resource.status() << " at "
            << location.file() << ":" << location.line();
        if (!event->resource.ok()) return std::nullopt;
        return ResourceAndReadDelayHandle{std::move(*event->resource),
                                          std::move(event->read_delay_handle)};
      }

      std::shared_ptr<const ResourceStruct> WaitForNextResource(
          SourceLocation location = SourceLocation()) {
        auto resource_and_handle = WaitForNextResourceAndHandle(location);
        if (!resource_and_handle.has_value()) return nullptr;
        return std::move(resource_and_handle->resource);
      }

      std::optional<absl::Status> WaitForNextError(
          SourceLocation location = SourceLocation()) {
        auto event = WaitForNextOnResourceChangedEvent(location);
        if (!event.has_value()) return std::nullopt;
        EXPECT_FALSE(event->resource.ok())
            << "got unexpected resource: " << (*event->resource)->name << " at "
            << location.file() << ":" << location.line();
        if (event->resource.ok()) return std::nullopt;
        return event->resource.status();
      }

      bool WaitForDoesNotExist(SourceLocation location = SourceLocation()) {
        auto status = WaitForNextError(location);
        if (!status.has_value()) return false;
        EXPECT_EQ(status->code(), absl::StatusCode::kNotFound)
            << "unexpected status: " << *status << " at " << location.file()
            << ":" << location.line();
        return status->code() == absl::StatusCode::kNotFound;
      }

      std::optional<absl::Status> WaitForNextAmbientError(
          SourceLocation location = SourceLocation()) {
        auto event = WaitForNextEvent();
        if (!event.has_value()) return std::nullopt;
        return Match(
            event->payload,
            [&](const absl::StatusOr<std::shared_ptr<const ResourceStruct>>&
                    resource) -> std::optional<absl::Status> {
              EXPECT_TRUE(false)
                  << "got unexpected resource: " << event->ToString() << " at "
                  << location.file() << ":" << location.line();
              return std::nullopt;
            },
            [&](const absl::Status& status) -> std::optional<absl::Status> {
              return status;
            });
      }

     private:
      // An event delivered to the watcher.
      struct Event {
        std::variant<
            // OnResourceChanged()
            absl::StatusOr<std::shared_ptr<const ResourceStruct>>,
            // OnAmbientError()
            absl::Status>
            payload;
        RefCountedPtr<XdsClient::ReadDelayHandle> read_delay_handle;

        std::string ToString() const {
          return Match(
              payload,
              [&](const absl::StatusOr<std::shared_ptr<const ResourceStruct>>&
                      resource) {
                return absl::StrCat(
                    "{resource=",
                    resource.ok() ? (*resource)->name
                                  : resource.status().ToString(),
                    ", read_delay_handle=", (read_delay_handle == nullptr),
                    "}");
              },
              [&](const absl::Status& status) {
                return absl::StrCat("{ambient_error=", status.ToString(),
                                    ", read_delay_handle=",
                                    (read_delay_handle == nullptr), "}");
              });
        }
      };

      std::optional<Event> WaitForNextEvent() {
        while (true) {
          {
            MutexLock lock(&mu_);
            if (!queue_.empty()) {
              Event event = std::move(queue_.front());
              queue_.pop_front();
              return event;
            }
            if (event_engine_->IsIdle()) return std::nullopt;
          }
          event_engine_->Tick();
        }
      }

      struct OnResourceChangedEvent {
        absl::StatusOr<std::shared_ptr<const ResourceStruct>> resource;
        RefCountedPtr<XdsClient::ReadDelayHandle> read_delay_handle;
      };
      std::optional<OnResourceChangedEvent> WaitForNextOnResourceChangedEvent(
          SourceLocation location = SourceLocation()) {
        auto event = WaitForNextEvent();
        if (!event.has_value()) return std::nullopt;
        return MatchMutable(
            &event->payload,
            [&](absl::StatusOr<std::shared_ptr<const ResourceStruct>>* resource)
                -> std::optional<OnResourceChangedEvent> {
              return OnResourceChangedEvent{
                  std::move(*resource), std::move(event->read_delay_handle)};
            },
            [&](absl::Status* status) -> std::optional<OnResourceChangedEvent> {
              EXPECT_TRUE(false)
                  << "got unexpected ambient error: " << status->ToString()
                  << " at " << location.file() << ":" << location.line();
              return std::nullopt;
            });
      }

      void OnResourceChanged(
          absl::StatusOr<std::shared_ptr<const ResourceStruct>> resource,
          RefCountedPtr<XdsClient::ReadDelayHandle> read_delay_handle)
          override {
        MutexLock lock(&mu_);
        queue_.emplace_back(
            Event{std::move(resource), std::move(read_delay_handle)});
      }

      void OnAmbientError(absl::Status status,
                          RefCountedPtr<XdsClient::ReadDelayHandle>
                              read_delay_handle) override {
        MutexLock lock(&mu_);
        queue_.push_back(
            Event{std::move(status), std::move(read_delay_handle)});
      }

      std::shared_ptr<FuzzingEventEngine> event_engine_;

      Mutex mu_;
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
      } else if (auto resource = LoadFromJson<ResourceStruct>(*json);
                 !resource.ok()) {
        if (auto it = json->object().find("name"); it != json->object().end()) {
          result.name = it->second.string();
        }
        result.resource = resource.status();
      } else {
        result.name = resource->name;
        result.resource =
            std::make_unique<ResourceStruct>(std::move(*resource));
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

    ResponseBuilder& AddResourceError(absl::string_view name,
                                      absl::Status status) {
      auto* error = response_.add_resource_errors();
      error->mutable_resource_name()->set_name(std::string(name));
      auto* status_proto = error->mutable_error_detail();
      status_proto->set_code(static_cast<uint32_t>(status.code()));
      status_proto->set_message(std::string(status.message()));
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

  class MetricsReporter : public XdsMetricsReporter {
   public:
    using ResourceUpdateMap = std::map<
        std::pair<std::string /*xds_server*/, std::string /*resource_type*/>,
        uint64_t>;
    using ServerFailureMap = std::map<std::string /*xds_server*/, uint64_t>;

    explicit MetricsReporter(std::shared_ptr<FuzzingEventEngine> event_engine)
        : event_engine_(std::move(event_engine)) {}

    ResourceUpdateMap resource_updates_valid() const {
      MutexLock lock(&mu_);
      return resource_updates_valid_;
    }
    ResourceUpdateMap resource_updates_invalid() const {
      MutexLock lock(&mu_);
      return resource_updates_invalid_;
    }
    const ServerFailureMap& server_failures() const { return server_failures_; }

    // Returns true if matchers return true before the timeout.
    // Runs matchers once as soon as it is called and then again
    // every time the metrics reporter sees an update.
    bool WaitForMetricsReporterData(
        ::testing::Matcher<ResourceUpdateMap> resource_updates_valid_matcher,
        ::testing::Matcher<ResourceUpdateMap> resource_updates_invalid_matcher,
        ::testing::Matcher<ServerFailureMap> server_failures_matcher,
        SourceLocation location = SourceLocation()) {
      while (true) {
        {
          MutexLock lock(&mu_);
          if (::testing::Matches(resource_updates_valid_matcher)(
                  resource_updates_valid_) &&
              ::testing::Matches(resource_updates_invalid_matcher)(
                  resource_updates_invalid_) &&
              ::testing::Matches(server_failures_matcher)(server_failures_)) {
            return true;
          }
          if (event_engine_->IsIdle()) {
            EXPECT_THAT(resource_updates_valid_, resource_updates_valid_matcher)
                << location.file() << ":" << location.line();
            EXPECT_THAT(resource_updates_invalid_,
                        resource_updates_invalid_matcher)
                << location.file() << ":" << location.line();
            EXPECT_THAT(server_failures_, server_failures_matcher)
                << location.file() << ":" << location.line();
            return false;
          }
        }
        event_engine_->Tick();
      }
    }

   private:
    void ReportResourceUpdates(absl::string_view xds_server,
                               absl::string_view resource_type,
                               uint64_t num_resources_valid,
                               uint64_t num_resources_invalid) override {
      MutexLock lock(&mu_);
      auto key = std::pair(std::string(xds_server), std::string(resource_type));
      if (num_resources_valid > 0) {
        resource_updates_valid_[key] += num_resources_valid;
      }
      if (num_resources_invalid > 0) {
        resource_updates_invalid_[key] += num_resources_invalid;
      }
      cond_.SignalAll();
    }

    void ReportServerFailure(absl::string_view xds_server) override {
      MutexLock lock(&mu_);
      ++server_failures_[std::string(xds_server)];
      cond_.SignalAll();
    }

    std::shared_ptr<FuzzingEventEngine> event_engine_;

    mutable Mutex mu_;
    ResourceUpdateMap resource_updates_valid_ ABSL_GUARDED_BY(mu_);
    ResourceUpdateMap resource_updates_invalid_ ABSL_GUARDED_BY(mu_);
    ServerFailureMap server_failures_ ABSL_GUARDED_BY(mu_);
    CondVar cond_;
  };

  using ResourceCounts =
      std::vector<std::pair<XdsClientTestPeer::ResourceCountLabels, uint64_t>>;
  ResourceCounts GetResourceCounts() {
    ResourceCounts resource_counts;
    XdsClientTestPeer(xds_client_.get())
        .TestReportResourceCounts(
            [&](const XdsClientTestPeer::ResourceCountLabels& labels,
                uint64_t count) {
              resource_counts.emplace_back(labels, count);
            });
    return resource_counts;
  }

  using ServerConnectionMap = std::map<std::string, bool>;
  ServerConnectionMap GetServerConnections() {
    ServerConnectionMap server_connection_map;
    XdsClientTestPeer(xds_client_.get())
        .TestReportServerConnections(
            [&](absl::string_view xds_server, bool connected) {
              std::string server(xds_server);
              EXPECT_EQ(server_connection_map.find(server),
                        server_connection_map.end());
              server_connection_map[std::move(server)] = connected;
            });
    return server_connection_map;
  }

  void SetUp() override {
    time_cache_.TestOnlySetNow(kTime0);
    event_engine_ = std::make_shared<FuzzingEventEngine>(
        FuzzingEventEngine::Options(), fuzzing_event_engine::Actions());
    grpc_timer_manager_set_start_threaded(false);
    grpc_init();
  }

  void TearDown() override {
    transport_factory_.reset();
    xds_client_.reset();
    event_engine_->FuzzingDone();
    event_engine_->TickUntilIdle();
    event_engine_->UnsetGlobalHooks();
    WaitForSingleOwner(std::move(event_engine_));
    grpc_shutdown_blocking();
  }

  // Sets transport_factory_ and initializes xds_client_ with the
  // specified bootstrap config.
  void InitXdsClient(FakeXdsBootstrap::Builder bootstrap_builder =
                         FakeXdsBootstrap::Builder()) {
    transport_factory_ = MakeRefCounted<FakeXdsTransportFactory>(
        []() { FAIL() << "Multiple concurrent reads"; }, event_engine_);
    auto metrics_reporter = std::make_unique<MetricsReporter>(event_engine_);
    metrics_reporter_ = metrics_reporter.get();
    xds_client_ = MakeRefCounted<XdsClient>(
        bootstrap_builder.Build(), transport_factory_, event_engine_,
        std::move(metrics_reporter), "foo agent", "foo version");
  }

  // Starts and cancels a watch for a Foo resource.
  RefCountedPtr<XdsFooResourceType::Watcher> StartFooWatch(
      absl::string_view resource_name) {
    auto watcher = MakeRefCounted<XdsFooResourceType::Watcher>(event_engine_);
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
    auto watcher = MakeRefCounted<XdsBarResourceType::Watcher>(event_engine_);
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
    auto watcher =
        MakeRefCounted<XdsWildcardCapableResourceType::Watcher>(event_engine_);
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
      const XdsBootstrap::XdsServer& xds_server) {
    return transport_factory_->WaitForStream(
        *xds_server.target(), FakeXdsTransportFactory::kAdsMethod);
  }

  RefCountedPtr<FakeXdsTransportFactory::FakeStreamingCall> WaitForAdsStream() {
    return WaitForAdsStream(*xds_client_->bootstrap().servers().front());
  }

  void TriggerConnectionFailure(const XdsBootstrap::XdsServer& xds_server,
                                absl::Status status) {
    transport_factory_->TriggerConnectionFailure(*xds_server.target(),
                                                 std::move(status));
  }

  // Gets the latest request sent to the fake xDS server.
  std::optional<DiscoveryRequest> WaitForRequest(
      FakeXdsTransportFactory::FakeStreamingCall* stream,
      SourceLocation location = SourceLocation()) {
    auto message = stream->WaitForMessageFromClient();
    if (!message.has_value()) return std::nullopt;
    DiscoveryRequest request;
    bool success = request.ParseFromString(*message);
    EXPECT_TRUE(success) << "Failed to deserialize DiscoveryRequest at "
                         << location.file() << ":" << location.line();
    if (!success) return std::nullopt;
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
    return CheckNode(request.node(), location);
  }

  // Helper function to check the contents of a node message against the
  // client's node info.
  void CheckNode(const envoy::config::core::v3::Node& node,
                 SourceLocation location = SourceLocation()) {
    // These fields come from the bootstrap config.
    EXPECT_EQ(node.id(), xds_client_->bootstrap().node()->id())
        << location.file() << ":" << location.line();
    EXPECT_EQ(node.cluster(), xds_client_->bootstrap().node()->cluster())
        << location.file() << ":" << location.line();
    EXPECT_EQ(node.locality().region(),
              xds_client_->bootstrap().node()->locality_region())
        << location.file() << ":" << location.line();
    EXPECT_EQ(node.locality().zone(),
              xds_client_->bootstrap().node()->locality_zone())
        << location.file() << ":" << location.line();
    EXPECT_EQ(node.locality().sub_zone(),
              xds_client_->bootstrap().node()->locality_sub_zone())
        << location.file() << ":" << location.line();
    if (xds_client_->bootstrap().node()->metadata().empty()) {
      EXPECT_FALSE(node.has_metadata())
          << location.file() << ":" << location.line();
    } else {
      std::string metadata_json_str;
      auto status =
          MessageToJsonString(node.metadata(), &metadata_json_str,
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
    EXPECT_EQ(node.user_agent_name(), "foo agent")
        << location.file() << ":" << location.line();
    EXPECT_EQ(node.user_agent_version(), "foo version")
        << location.file() << ":" << location.line();
  }

  ClientConfig DumpCsds(SourceLocation location = SourceLocation()) {
    std::string client_config_serialized =
        XdsClientTestPeer(xds_client_.get()).TestDumpClientConfig();
    ClientConfig client_config_proto;
    CHECK(client_config_proto.ParseFromString(client_config_serialized))
        << "at " << location.file() << ":" << location.line();
    CheckNode(client_config_proto.node(), location);
    return client_config_proto;
  }

  ScopedTimeCache time_cache_;
  std::shared_ptr<FuzzingEventEngine> event_engine_;
  RefCountedPtr<FakeXdsTransportFactory> transport_factory_;
  RefCountedPtr<XdsClient> xds_client_;
  MetricsReporter* metrics_reporter_ = nullptr;
};

MATCHER_P3(ResourceCountLabelsEq, xds_authority, resource_type, cache_state,
           "equals ResourceCountLabels") {
  bool ok = true;
  ok &= ::testing::ExplainMatchResult(xds_authority, arg.xds_authority,
                                      result_listener);
  ok &= ::testing::ExplainMatchResult(resource_type, arg.resource_type,
                                      result_listener);
  ok &= ::testing::ExplainMatchResult(cache_state, arg.cache_state,
                                      result_listener);
  return ok;
}

MATCHER_P(TimestampProtoEq, timestamp, "equals timestamp") {
  gpr_timespec ts = {arg.seconds(), arg.nanos(), GPR_CLOCK_REALTIME};
  return ::testing::ExplainMatchResult(
      timestamp, Timestamp::FromTimespecRoundDown(ts), result_listener);
}

// Matches a CSDS ClientConfig proto.
//
// The resource_fields argument is a matcher that must validate the
// xds_config, version_info, and last_updated fields.  Examples are
// CsdsResourceFields() and CsdsNoResourceFields().
//
// The error_fields argument is a matcher that must validate the
// error_state field.  Examples are CsdsErrorFields(),
// CsdsErrorDetailsOnly(), and CsdsNoErrorFields().
MATCHER_P5(CsdsResourceEq, client_status, type_url, name, resource_fields,
           error_fields, "equals CSDS resource") {
  bool ok = true;
  ok &= ::testing::ExplainMatchResult(arg.client_status(), client_status,
                                      result_listener);
  ok &= ::testing::ExplainMatchResult(
      absl::StrCat("type.googleapis.com/", type_url), arg.type_url(),
      result_listener);
  ok &= ::testing::ExplainMatchResult(name, arg.name(), result_listener);
  ok &= ::testing::ExplainMatchResult(resource_fields, arg, result_listener);
  ok &= ::testing::ExplainMatchResult(error_fields, arg, result_listener);
  return ok;
}

// Validates the resource fields in a CSDS ClientConfig proto.  Intended
// for use with CsdsResourceEq().
MATCHER_P3(CsdsResourceFields, resource, version, last_updated,
           "CSDS resource fields") {
  bool ok = true;
  ok &= ::testing::ExplainMatchResult(resource, arg.xds_config().value(),
                                      result_listener);
  ok &= ::testing::ExplainMatchResult(version, arg.version_info(),
                                      result_listener);
  ok &= ::testing::ExplainMatchResult(last_updated, arg.last_updated(),
                                      result_listener);
  return ok;
}

// Validates the resource fields are not present in a CSDS ClientConfig
// proto.  Intended for use with CsdsResourceEq().
MATCHER(CsdsNoResourceFields, "CSDS has no resource fields") {
  bool ok = true;
  ok &= ::testing::ExplainMatchResult(::testing::IsFalse(),
                                      arg.has_xds_config(), result_listener);
  ok &= ::testing::ExplainMatchResult("", arg.version_info(), result_listener);
  ok &= ::testing::ExplainMatchResult(::testing::IsFalse(),
                                      arg.has_last_updated(), result_listener);
  return ok;
}

// Validates the error fields in a CSDS ClientConfig proto.  Intended
// for use with CsdsResourceEq().
MATCHER_P3(CsdsErrorFields, error_details, error_version, error_time,
           "CSDS error fields") {
  bool ok = true;
  ok &= ::testing::ExplainMatchResult(
      error_details, arg.error_state().details(), result_listener);
  ok &= ::testing::ExplainMatchResult(
      error_version, arg.error_state().version_info(), result_listener);
  ok &= ::testing::ExplainMatchResult(
      error_time, arg.error_state().last_update_attempt(), result_listener);
  return ok;
}

// Same as CsdsErrorFields, but expects the error details without a
// version or timestamp.
MATCHER_P(CsdsErrorDetailsOnly, error_details, "CSDS error details only") {
  bool ok = true;
  ok &= ::testing::ExplainMatchResult(
      error_details, arg.error_state().details(), result_listener);
  ok &= ::testing::ExplainMatchResult("", arg.error_state().version_info(),
                                      result_listener);
  ok &= ::testing::ExplainMatchResult(
      ::testing::IsFalse(), arg.error_state().has_last_update_attempt(),
      result_listener);
  return ok;
}

// Validates that there is no error in a CSDS ClientConfig proto.  Intended
// for use with CsdsResourceEq().
MATCHER(CsdsNoErrorFields, "CSDS has no error fields") {
  return ::testing::ExplainMatchResult(::testing::IsFalse(),
                                       arg.has_error_state(), result_listener);
}

// Convenient wrapper for ACKED resources in CSDS.
MATCHER_P5(CsdsResourceAcked, type_url, name, resource, version, last_updated,
           "equals CSDS ACKED resource") {
  return ::testing::ExplainMatchResult(
      CsdsResourceEq(ClientResourceStatus::ACKED, type_url, name,
                     CsdsResourceFields(resource, version, last_updated),
                     CsdsNoErrorFields()),
      arg, result_listener);
}

// Convenient wrapper for REQUESTED resources in CSDS.
MATCHER_P2(CsdsResourceRequested, type_url, name,
           "equals CSDS requested resource") {
  return ::testing::ExplainMatchResult(
      CsdsResourceEq(ClientResourceStatus::REQUESTED, type_url, name,
                     CsdsNoResourceFields(), CsdsNoErrorFields()),
      arg, result_listener);
}

// Convenient wrapper for DOES_NOT_EXIST resources in CSDS caused by
// the resource timer.
MATCHER_P2(CsdsResourceDoesNotExistOnTimeout, type_url, name,
           "equals CSDS does-not-exist-on-timeout resource") {
  return ::testing::ExplainMatchResult(
      CsdsResourceEq(ClientResourceStatus::DOES_NOT_EXIST, type_url, name,
                     CsdsNoResourceFields(),
                     CsdsErrorDetailsOnly("does not exist")),
      arg, result_listener);
}

TEST_F(XdsClientTest, BasicWatch) {
  InitXdsClient();
  // Metrics should initially be empty.
  EXPECT_THAT(metrics_reporter_->resource_updates_valid(),
              ::testing::ElementsAre());
  EXPECT_THAT(metrics_reporter_->resource_updates_invalid(),
              ::testing::ElementsAre());
  EXPECT_THAT(GetResourceCounts(), ::testing::ElementsAre());
  EXPECT_THAT(GetServerConnections(), ::testing::ElementsAre());
  EXPECT_THAT(metrics_reporter_->server_failures(), ::testing::ElementsAre());
  // CSDS should initially be empty.
  ClientConfig csds = DumpCsds();
  EXPECT_THAT(csds.generic_xds_configs(), ::testing::ElementsAre());
  // Start a watch for "foo1".
  auto watcher = StartFooWatch("foo1");
  // Check metrics.
  EXPECT_THAT(metrics_reporter_->resource_updates_valid(),
              ::testing::ElementsAre());
  EXPECT_THAT(metrics_reporter_->resource_updates_invalid(),
              ::testing::ElementsAre());
  EXPECT_THAT(GetServerConnections(), ::testing::ElementsAre(::testing::Pair(
                                          kDefaultXdsServerUrl, true)));
  EXPECT_THAT(GetResourceCounts(),
              ::testing::ElementsAre(::testing::Pair(
                  ResourceCountLabelsEq(XdsClient::kOldStyleAuthority,
                                        XdsFooResourceType::Get()->type_url(),
                                        "requested"),
                  1)));
  // CSDS should show that the resource has been requested.
  csds = DumpCsds();
  EXPECT_THAT(csds.generic_xds_configs(),
              ::testing::ElementsAre(CsdsResourceRequested(
                  XdsFooResourceType::Get()->type_url(), "foo1")));
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
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  // Check metric data.
  EXPECT_TRUE(metrics_reporter_->WaitForMetricsReporterData(
      ::testing::ElementsAre(::testing::Pair(
          ::testing::Pair(kDefaultXdsServerUrl,
                          XdsFooResourceType::Get()->type_url()),
          1)),
      ::testing::ElementsAre(), ::testing::_));
  EXPECT_THAT(
      GetResourceCounts(),
      ::testing::ElementsAre(::testing::Pair(
          ResourceCountLabelsEq(XdsClient::kOldStyleAuthority,
                                XdsFooResourceType::Get()->type_url(), "acked"),
          1)));
  EXPECT_THAT(GetServerConnections(), ::testing::ElementsAre(::testing::Pair(
                                          kDefaultXdsServerUrl, true)));
  // Check CSDS data.
  csds = DumpCsds();
  EXPECT_THAT(csds.generic_xds_configs(),
              ::testing::ElementsAre(CsdsResourceAcked(
                  XdsFooResourceType::Get()->type_url(), "foo1",
                  resource->AsJsonString(), "1", TimestampProtoEq(kTime0))));
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  // Cancel watch.
  CancelFooWatch(watcher.get(), "foo1");
  EXPECT_TRUE(stream->IsOrphaned());
  // Check metric data.
  EXPECT_TRUE(metrics_reporter_->WaitForMetricsReporterData(
      ::testing::ElementsAre(::testing::Pair(
          ::testing::Pair(kDefaultXdsServerUrl,
                          XdsFooResourceType::Get()->type_url()),
          1)),
      ::testing::ElementsAre(), ::testing::_));
  EXPECT_THAT(GetResourceCounts(), ::testing::ElementsAre());
  EXPECT_THAT(GetServerConnections(), ::testing::ElementsAre());
  // Check CSDS data.
  csds = DumpCsds();
  EXPECT_THAT(csds.generic_xds_configs(), ::testing::ElementsAre());
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
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  // Check metric data.
  EXPECT_TRUE(metrics_reporter_->WaitForMetricsReporterData(
      ::testing::ElementsAre(::testing::Pair(
          ::testing::Pair(kDefaultXdsServerUrl,
                          XdsFooResourceType::Get()->type_url()),
          1)),
      ::testing::ElementsAre(), ::testing::_));
  EXPECT_THAT(
      GetResourceCounts(),
      ::testing::ElementsAre(::testing::Pair(
          ResourceCountLabelsEq(XdsClient::kOldStyleAuthority,
                                XdsFooResourceType::Get()->type_url(), "acked"),
          1)));
  // Check CSDS data.
  ClientConfig csds = DumpCsds();
  EXPECT_THAT(csds.generic_xds_configs(),
              ::testing::ElementsAre(CsdsResourceAcked(
                  XdsFooResourceType::Get()->type_url(), "foo1",
                  resource->AsJsonString(), "1", TimestampProtoEq(kTime0))));
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  // Server sends an updated version of the resource.
  // We increment time to make sure that the CSDS data gets a new timestamp.
  time_cache_.TestOnlySetNow(kTime1);
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("2")
          .set_nonce("B")
          .AddFooResource(XdsFooResource("foo1", 9))
          .Serialize());
  // XdsClient should have delivered the response to the watcher.
  resource = watcher->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 9);
  // Check metric data.
  EXPECT_TRUE(metrics_reporter_->WaitForMetricsReporterData(
      ::testing::ElementsAre(::testing::Pair(
          ::testing::Pair(kDefaultXdsServerUrl,
                          XdsFooResourceType::Get()->type_url()),
          2)),
      ::testing::ElementsAre(), ::testing::_));
  EXPECT_THAT(
      GetResourceCounts(),
      ::testing::ElementsAre(::testing::Pair(
          ResourceCountLabelsEq(XdsClient::kOldStyleAuthority,
                                XdsFooResourceType::Get()->type_url(), "acked"),
          1)));
  // Check CSDS data.
  csds = DumpCsds();
  EXPECT_THAT(csds.generic_xds_configs(),
              ::testing::ElementsAre(CsdsResourceAcked(
                  XdsFooResourceType::Get()->type_url(), "foo1",
                  resource->AsJsonString(), "2", TimestampProtoEq(kTime1))));
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"2", /*response_nonce=*/"B",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  // Cancel watch.
  CancelFooWatch(watcher.get(), "foo1");
  EXPECT_TRUE(stream->IsOrphaned());
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
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  // Check metric data.
  EXPECT_TRUE(metrics_reporter_->WaitForMetricsReporterData(
      ::testing::ElementsAre(::testing::Pair(
          ::testing::Pair(kDefaultXdsServerUrl,
                          XdsFooResourceType::Get()->type_url()),
          1)),
      ::testing::ElementsAre(), ::testing::_));
  EXPECT_THAT(
      GetResourceCounts(),
      ::testing::ElementsAre(::testing::Pair(
          ResourceCountLabelsEq(XdsClient::kOldStyleAuthority,
                                XdsFooResourceType::Get()->type_url(), "acked"),
          1)));
  // Check CSDS data.
  ClientConfig csds = DumpCsds();
  EXPECT_THAT(csds.generic_xds_configs(),
              ::testing::ElementsAre(CsdsResourceAcked(
                  XdsFooResourceType::Get()->type_url(), "foo1",
                  resource->AsJsonString(), "1", TimestampProtoEq(kTime0))));
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
  ASSERT_NE(resource, nullptr);
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
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 9);
  resource = watcher2->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 9);
  // Check metric data.
  EXPECT_TRUE(metrics_reporter_->WaitForMetricsReporterData(
      ::testing::ElementsAre(::testing::Pair(
          ::testing::Pair(kDefaultXdsServerUrl,
                          XdsFooResourceType::Get()->type_url()),
          2)),
      ::testing::ElementsAre(), ::testing::_));
  EXPECT_THAT(
      GetResourceCounts(),
      ::testing::ElementsAre(::testing::Pair(
          ResourceCountLabelsEq(XdsClient::kOldStyleAuthority,
                                XdsFooResourceType::Get()->type_url(), "acked"),
          1)));
  // Check CSDS data.
  csds = DumpCsds();
  EXPECT_THAT(csds.generic_xds_configs(),
              ::testing::ElementsAre(CsdsResourceAcked(
                  XdsFooResourceType::Get()->type_url(), "foo1",
                  resource->AsJsonString(), "2", TimestampProtoEq(kTime0))));
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
  EXPECT_TRUE(stream->IsOrphaned());
}

TEST_F(XdsClientTest, SubscribeToMultipleResources) {
  InitXdsClient();
  // Start a watch for "foo1".
  auto watcher = StartFooWatch("foo1");
  // Watcher should initially not see any resource reported.
  EXPECT_FALSE(watcher->HasEvent());
  // Check metrics.
  EXPECT_THAT(metrics_reporter_->resource_updates_valid(),
              ::testing::ElementsAre());
  EXPECT_THAT(metrics_reporter_->resource_updates_invalid(),
              ::testing::ElementsAre());
  EXPECT_THAT(GetResourceCounts(),
              ::testing::ElementsAre(::testing::Pair(
                  ResourceCountLabelsEq(XdsClient::kOldStyleAuthority,
                                        XdsFooResourceType::Get()->type_url(),
                                        "requested"),
                  1)));
  // CSDS should show that the resource has been requested.
  ClientConfig csds = DumpCsds();
  EXPECT_THAT(csds.generic_xds_configs(),
              ::testing::ElementsAre(CsdsResourceRequested(
                  XdsFooResourceType::Get()->type_url(), "foo1")));
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
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  // Check metric data.
  EXPECT_TRUE(metrics_reporter_->WaitForMetricsReporterData(
      ::testing::ElementsAre(::testing::Pair(
          ::testing::Pair(kDefaultXdsServerUrl,
                          XdsFooResourceType::Get()->type_url()),
          1)),
      ::testing::ElementsAre(), ::testing::_));
  EXPECT_THAT(
      GetResourceCounts(),
      ::testing::ElementsAre(::testing::Pair(
          ResourceCountLabelsEq(XdsClient::kOldStyleAuthority,
                                XdsFooResourceType::Get()->type_url(), "acked"),
          1)));
  // Check CSDS data.
  csds = DumpCsds();
  EXPECT_THAT(csds.generic_xds_configs(),
              ::testing::ElementsAre(CsdsResourceAcked(
                  XdsFooResourceType::Get()->type_url(), "foo1",
                  resource->AsJsonString(), "1", TimestampProtoEq(kTime0))));
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  // Start a watch for "foo2".
  auto watcher2 = StartFooWatch("foo2");
  // Check metric data.
  EXPECT_THAT(
      GetResourceCounts(),
      ::testing::ElementsAre(
          ::testing::Pair(ResourceCountLabelsEq(
                              XdsClient::kOldStyleAuthority,
                              XdsFooResourceType::Get()->type_url(), "acked"),
                          1),
          ::testing::Pair(
              ResourceCountLabelsEq(XdsClient::kOldStyleAuthority,
                                    XdsFooResourceType::Get()->type_url(),
                                    "requested"),
              1)));
  // Check CSDS data.
  csds = DumpCsds();
  EXPECT_THAT(csds.generic_xds_configs(),
              ::testing::UnorderedElementsAre(
                  CsdsResourceAcked(XdsFooResourceType::Get()->type_url(),
                                    "foo1", resource->AsJsonString(), "1",
                                    TimestampProtoEq(kTime0)),
                  CsdsResourceRequested(XdsFooResourceType::Get()->type_url(),
                                        "foo2")));
  // XdsClient should have sent a subscription request on the ADS stream.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1", "foo2"});
  // Send a response.
  // We increment time to make sure that the CSDS data gets a new timestamp.
  time_cache_.TestOnlySetNow(kTime1);
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("1")
          .set_nonce("B")
          .AddFooResource(XdsFooResource("foo2", 7))
          .Serialize());
  // XdsClient should have delivered the response to the watcher.
  auto resource2 = watcher2->WaitForNextResource();
  ASSERT_NE(resource2, nullptr);
  EXPECT_EQ(resource2->name, "foo2");
  EXPECT_EQ(resource2->value, 7);
  // Check metric data.
  EXPECT_TRUE(metrics_reporter_->WaitForMetricsReporterData(
      ::testing::ElementsAre(::testing::Pair(
          ::testing::Pair(kDefaultXdsServerUrl,
                          XdsFooResourceType::Get()->type_url()),
          2)),
      ::testing::ElementsAre(), ::testing::_));
  EXPECT_THAT(
      GetResourceCounts(),
      ::testing::ElementsAre(::testing::Pair(
          ResourceCountLabelsEq(XdsClient::kOldStyleAuthority,
                                XdsFooResourceType::Get()->type_url(), "acked"),
          2)));
  // Check CSDS data.
  csds = DumpCsds();
  EXPECT_THAT(csds.generic_xds_configs(),
              ::testing::UnorderedElementsAre(
                  CsdsResourceAcked(XdsFooResourceType::Get()->type_url(),
                                    "foo1", resource->AsJsonString(), "1",
                                    TimestampProtoEq(kTime0)),
                  CsdsResourceAcked(XdsFooResourceType::Get()->type_url(),
                                    "foo2", resource2->AsJsonString(), "1",
                                    TimestampProtoEq(kTime1))));
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"B",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1", "foo2"});
  // Cancel watch for "foo1".
  CancelFooWatch(watcher.get(), "foo1");
  // Check metric data.
  EXPECT_THAT(
      GetResourceCounts(),
      ::testing::ElementsAre(::testing::Pair(
          ResourceCountLabelsEq(XdsClient::kOldStyleAuthority,
                                XdsFooResourceType::Get()->type_url(), "acked"),
          1)));
  // Check CSDS data.
  csds = DumpCsds();
  EXPECT_THAT(csds.generic_xds_configs(),
              ::testing::ElementsAre(CsdsResourceAcked(
                  XdsFooResourceType::Get()->type_url(), "foo2",
                  resource2->AsJsonString(), "1", TimestampProtoEq(kTime1))));
  // XdsClient should send an unsubscription request.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"B",
               /*error_detail=*/absl::OkStatus(), /*resource_names=*/{"foo2"});
  // Now cancel watch for "foo2".
  CancelFooWatch(watcher2.get(), "foo2");
  EXPECT_TRUE(stream->IsOrphaned());
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
  ASSERT_NE(resource, nullptr);
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
  // We increment time to make sure that the CSDS data gets a new timestamp.
  time_cache_.TestOnlySetNow(kTime1);
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("1")
          .set_nonce("B")
          .AddFooResource(XdsFooResource("foo2", 7))
          .Serialize());
  // XdsClient should have delivered the response to the watcher.
  auto resource2 = watcher2->WaitForNextResource();
  ASSERT_NE(resource2, nullptr);
  EXPECT_EQ(resource2->name, "foo2");
  EXPECT_EQ(resource2->value, 7);
  // Check metric data.
  EXPECT_TRUE(metrics_reporter_->WaitForMetricsReporterData(
      ::testing::ElementsAre(::testing::Pair(
          ::testing::Pair(kDefaultXdsServerUrl,
                          XdsFooResourceType::Get()->type_url()),
          2)),
      ::testing::ElementsAre(), ::testing::_));
  EXPECT_THAT(
      GetResourceCounts(),
      ::testing::ElementsAre(::testing::Pair(
          ResourceCountLabelsEq(XdsClient::kOldStyleAuthority,
                                XdsFooResourceType::Get()->type_url(), "acked"),
          2)));
  // Check CSDS data.
  ClientConfig csds = DumpCsds();
  EXPECT_THAT(csds.generic_xds_configs(),
              ::testing::UnorderedElementsAre(
                  CsdsResourceAcked(XdsFooResourceType::Get()->type_url(),
                                    "foo1", resource->AsJsonString(), "1",
                                    TimestampProtoEq(kTime0)),
                  CsdsResourceAcked(XdsFooResourceType::Get()->type_url(),
                                    "foo2", resource2->AsJsonString(), "1",
                                    TimestampProtoEq(kTime1))));
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"B",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1", "foo2"});
  // Server sends an update for "foo1".  The response does not contain "foo2".
  // We increment time to make sure that the CSDS data gets a new timestamp.
  time_cache_.TestOnlySetNow(kTime2);
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("2")
          .set_nonce("C")
          .AddFooResource(XdsFooResource("foo1", 9))
          .Serialize());
  // XdsClient should have delivered the response to the watcher.
  resource = watcher->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 9);
  // Check metric data.
  EXPECT_TRUE(metrics_reporter_->WaitForMetricsReporterData(
      ::testing::ElementsAre(::testing::Pair(
          ::testing::Pair(kDefaultXdsServerUrl,
                          XdsFooResourceType::Get()->type_url()),
          3)),
      ::testing::ElementsAre(), ::testing::_));
  EXPECT_THAT(
      GetResourceCounts(),
      ::testing::ElementsAre(::testing::Pair(
          ResourceCountLabelsEq(XdsClient::kOldStyleAuthority,
                                XdsFooResourceType::Get()->type_url(), "acked"),
          2)));
  // Check CSDS data.
  csds = DumpCsds();
  EXPECT_THAT(csds.generic_xds_configs(),
              ::testing::UnorderedElementsAre(
                  CsdsResourceAcked(XdsFooResourceType::Get()->type_url(),
                                    "foo1", resource->AsJsonString(), "2",
                                    TimestampProtoEq(kTime2)),
                  CsdsResourceAcked(XdsFooResourceType::Get()->type_url(),
                                    "foo2", resource2->AsJsonString(), "1",
                                    TimestampProtoEq(kTime1))));
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
  EXPECT_TRUE(stream->IsOrphaned());
}

TEST_F(XdsClientTest, ResourceValidationFailure) {
  InitXdsClient();
  // Start a watch for "foo1".
  auto watcher = StartFooWatch("foo1");
  // Check metric data.
  EXPECT_THAT(metrics_reporter_->resource_updates_valid(),
              ::testing::ElementsAre());
  EXPECT_THAT(metrics_reporter_->resource_updates_invalid(),
              ::testing::ElementsAre());
  EXPECT_THAT(GetResourceCounts(),
              ::testing::ElementsAre(::testing::Pair(
                  ResourceCountLabelsEq(XdsClient::kOldStyleAuthority,
                                        XdsFooResourceType::Get()->type_url(),
                                        "requested"),
                  1)));
  // CSDS should show that the resource has been requested.
  ClientConfig csds = DumpCsds();
  EXPECT_THAT(csds.generic_xds_configs(),
              ::testing::ElementsAre(CsdsResourceRequested(
                  XdsFooResourceType::Get()->type_url(), "foo1")));
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
  EXPECT_EQ(error->code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(error->message(),
            "invalid resource: errors validating JSON: "
            "[field:value error:is not a number] (node ID:xds_client_test)")
      << *error;
  // Check metric data.
  EXPECT_TRUE(metrics_reporter_->WaitForMetricsReporterData(
      ::testing::ElementsAre(),
      ::testing::ElementsAre(::testing::Pair(
          ::testing::Pair(kDefaultXdsServerUrl,
                          XdsFooResourceType::Get()->type_url()),
          1)),
      ::testing::_));
  EXPECT_THAT(GetResourceCounts(),
              ::testing::ElementsAre(::testing::Pair(
                  ResourceCountLabelsEq(XdsClient::kOldStyleAuthority,
                                        XdsFooResourceType::Get()->type_url(),
                                        "nacked"),
                  1)));
  // CSDS should show that the resource has been NACKed.
  csds = DumpCsds();
  EXPECT_THAT(
      csds.generic_xds_configs(),
      ::testing::ElementsAre(CsdsResourceEq(
          ClientResourceStatus::NACKED, XdsFooResourceType::Get()->type_url(),
          "foo1", CsdsNoResourceFields(),
          CsdsErrorFields("invalid resource: errors validating JSON: "
                          "[field:value error:is not a number]",
                          "1", TimestampProtoEq(kTime0)))));
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
  EXPECT_EQ(error->code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(error->message(),
            "invalid resource: errors validating JSON: "
            "[field:value error:is not a number] (node ID:xds_client_test)")
      << *error;
  // Now server sends an updated version of the resource.
  // We increment time to make sure that the CSDS data gets a new timestamp.
  time_cache_.TestOnlySetNow(kTime1);
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("2")
          .set_nonce("B")
          .AddFooResource(XdsFooResource("foo1", 9))
          .Serialize());
  // XdsClient should deliver the response to both watchers.
  auto resource = watcher->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 9);
  resource = watcher2->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 9);
  // Check metric data.
  EXPECT_TRUE(metrics_reporter_->WaitForMetricsReporterData(
      ::testing::ElementsAre(::testing::Pair(
          ::testing::Pair(kDefaultXdsServerUrl,
                          XdsFooResourceType::Get()->type_url()),
          1)),
      ::testing::ElementsAre(::testing::Pair(
          ::testing::Pair(kDefaultXdsServerUrl,
                          XdsFooResourceType::Get()->type_url()),
          1)),
      ::testing::_));
  EXPECT_THAT(
      GetResourceCounts(),
      ::testing::ElementsAre(::testing::Pair(
          ResourceCountLabelsEq(XdsClient::kOldStyleAuthority,
                                XdsFooResourceType::Get()->type_url(), "acked"),
          1)));
  // Check CSDS data.
  csds = DumpCsds();
  EXPECT_THAT(csds.generic_xds_configs(),
              ::testing::UnorderedElementsAre(CsdsResourceAcked(
                  XdsFooResourceType::Get()->type_url(), "foo1",
                  resource->AsJsonString(), "2", TimestampProtoEq(kTime1))));
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
  EXPECT_TRUE(stream->IsOrphaned());
}

TEST_F(XdsClientTest, ResourceValidationFailureMultipleResources) {
  InitXdsClient();
  // Start a watch for "foo1".
  auto watcher = StartFooWatch("foo1");
  // Watcher should initially not see any resource reported.
  EXPECT_FALSE(watcher->HasEvent());
  // Check metric data.
  EXPECT_THAT(metrics_reporter_->resource_updates_valid(),
              ::testing::ElementsAre());
  EXPECT_THAT(metrics_reporter_->resource_updates_invalid(),
              ::testing::ElementsAre());
  EXPECT_THAT(GetResourceCounts(),
              ::testing::ElementsAre(::testing::Pair(
                  ResourceCountLabelsEq(XdsClient::kOldStyleAuthority,
                                        XdsFooResourceType::Get()->type_url(),
                                        "requested"),
                  1)));
  // CSDS should show that the resource has been requested.
  ClientConfig csds = DumpCsds();
  EXPECT_THAT(csds.generic_xds_configs(),
              ::testing::ElementsAre(CsdsResourceRequested(
                  XdsFooResourceType::Get()->type_url(), "foo1")));
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
  // Check metric data.
  EXPECT_TRUE(metrics_reporter_->WaitForMetricsReporterData(
      ::testing::ElementsAre(), ::testing::ElementsAre(), ::testing::_));
  EXPECT_THAT(GetResourceCounts(),
              ::testing::ElementsAre(::testing::Pair(
                  ResourceCountLabelsEq(XdsClient::kOldStyleAuthority,
                                        XdsFooResourceType::Get()->type_url(),
                                        "requested"),
                  2)));
  // CSDS should show that both resourcees have been requested.
  csds = DumpCsds();
  EXPECT_THAT(
      csds.generic_xds_configs(),
      ::testing::ElementsAre(
          CsdsResourceRequested(XdsFooResourceType::Get()->type_url(), "foo1"),
          CsdsResourceRequested(XdsFooResourceType::Get()->type_url(),
                                "foo2")));
  // Client should send another request.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1", "foo2"});
  // Add a watch for a third resource.
  auto watcher3 = StartFooWatch("foo3");
  // Check metric data.
  EXPECT_TRUE(metrics_reporter_->WaitForMetricsReporterData(
      ::testing::ElementsAre(), ::testing::ElementsAre(), ::testing::_));
  EXPECT_THAT(GetResourceCounts(),
              ::testing::ElementsAre(::testing::Pair(
                  ResourceCountLabelsEq(XdsClient::kOldStyleAuthority,
                                        XdsFooResourceType::Get()->type_url(),
                                        "requested"),
                  3)));
  // Check CSDS.
  csds = DumpCsds();
  EXPECT_THAT(
      csds.generic_xds_configs(),
      ::testing::UnorderedElementsAre(
          CsdsResourceRequested(XdsFooResourceType::Get()->type_url(), "foo1"),
          CsdsResourceRequested(XdsFooResourceType::Get()->type_url(), "foo2"),
          CsdsResourceRequested(XdsFooResourceType::Get()->type_url(),
                                "foo3")));
  // Client should send another request.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1", "foo2", "foo3"});
  // Add a watch for a fourth resource.
  auto watcher4 = StartFooWatch("foo4");
  // Check metric data.
  EXPECT_TRUE(metrics_reporter_->WaitForMetricsReporterData(
      ::testing::ElementsAre(), ::testing::ElementsAre(), ::testing::_));
  EXPECT_THAT(GetResourceCounts(),
              ::testing::ElementsAre(::testing::Pair(
                  ResourceCountLabelsEq(XdsClient::kOldStyleAuthority,
                                        XdsFooResourceType::Get()->type_url(),
                                        "requested"),
                  4)));
  // Check CSDS.
  csds = DumpCsds();
  EXPECT_THAT(
      csds.generic_xds_configs(),
      ::testing::UnorderedElementsAre(
          CsdsResourceRequested(XdsFooResourceType::Get()->type_url(), "foo1"),
          CsdsResourceRequested(XdsFooResourceType::Get()->type_url(), "foo2"),
          CsdsResourceRequested(XdsFooResourceType::Get()->type_url(), "foo3"),
          CsdsResourceRequested(XdsFooResourceType::Get()->type_url(),
                                "foo4")));
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
          // Invalid resource wrapper.  Will be included in NACK but
          // will not affect any watchers.
          .AddInvalidResourceWrapper()
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
  EXPECT_EQ(error->code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(error->message(),
            "invalid resource: errors validating JSON: "
            "[field:value error:is not a number] (node ID:xds_client_test)")
      << *error;
  error = watcher3->WaitForNextError();
  ASSERT_TRUE(error.has_value());
  EXPECT_EQ(error->code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(error->message(),
            "invalid resource: JSON parsing failed: "
            "[JSON parse error at index 15] (node ID:xds_client_test)")
      << *error;
  // It cannot delivery an error for foo2, because the client doesn't know
  // that that resource in the response was actually supposed to be foo2.
  EXPECT_FALSE(watcher2->HasEvent());
  // It will delivery a valid resource update for foo4.
  auto resource = watcher4->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo4");
  EXPECT_EQ(resource->value, 5);
  // Check metric data.
  EXPECT_TRUE(metrics_reporter_->WaitForMetricsReporterData(
      ::testing::ElementsAre(::testing::Pair(
          ::testing::Pair(kDefaultXdsServerUrl,
                          XdsFooResourceType::Get()->type_url()),
          1)),
      ::testing::ElementsAre(::testing::Pair(
          ::testing::Pair(kDefaultXdsServerUrl,
                          XdsFooResourceType::Get()->type_url()),
          5)),
      ::testing::_));
  EXPECT_THAT(
      GetResourceCounts(),
      ::testing::ElementsAre(
          // foo4
          ::testing::Pair(ResourceCountLabelsEq(
                              XdsClient::kOldStyleAuthority,
                              XdsFooResourceType::Get()->type_url(), "acked"),
                          1),
          // foo1 and foo3
          ::testing::Pair(ResourceCountLabelsEq(
                              XdsClient::kOldStyleAuthority,
                              XdsFooResourceType::Get()->type_url(), "nacked"),
                          2),
          // did not recognize response for foo2
          ::testing::Pair(
              ResourceCountLabelsEq(XdsClient::kOldStyleAuthority,
                                    XdsFooResourceType::Get()->type_url(),
                                    "requested"),
              1)));
  // Check CSDS.
  csds = DumpCsds();
  EXPECT_THAT(
      csds.generic_xds_configs(),
      ::testing::UnorderedElementsAre(
          CsdsResourceEq(
              ClientResourceStatus::NACKED,
              XdsFooResourceType::Get()->type_url(), "foo1",
              CsdsNoResourceFields(),
              CsdsErrorFields("invalid resource: errors validating JSON: "
                              "[field:value error:is not a number]",
                              "1", TimestampProtoEq(kTime0))),
          CsdsResourceRequested(XdsFooResourceType::Get()->type_url(), "foo2"),
          CsdsResourceEq(
              ClientResourceStatus::NACKED,
              XdsFooResourceType::Get()->type_url(), "foo3",
              CsdsNoResourceFields(),
              CsdsErrorFields("invalid resource: JSON parsing failed: "
                              "[JSON parse error at index 15]",
                              "1", TimestampProtoEq(kTime0))),
          CsdsResourceAcked(XdsFooResourceType::Get()->type_url(), "foo4",
                            resource->AsJsonString(), "1",
                            TimestampProtoEq(kTime0))));
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
                   // invalid resource wrapper
                   "resource index 3: Can't decode Resource proto wrapper; "
                   // foo3
                   "resource index 4: foo3: "
                   "INVALID_ARGUMENT: JSON parsing failed: "
                   "[JSON parse error at index 15]]")),
               /*resource_names=*/{"foo1", "foo2", "foo3", "foo4"});
  // Cancel watches.
  CancelFooWatch(watcher.get(), "foo1", /*delay_unsubscription=*/true);
  CancelFooWatch(watcher2.get(), "foo2", /*delay_unsubscription=*/true);
  CancelFooWatch(watcher3.get(), "foo3", /*delay_unsubscription=*/true);
  CancelFooWatch(watcher4.get(), "foo4");
  EXPECT_TRUE(stream->IsOrphaned());
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
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  // Check metric data.
  EXPECT_TRUE(metrics_reporter_->WaitForMetricsReporterData(
      ::testing::ElementsAre(::testing::Pair(
          ::testing::Pair(kDefaultXdsServerUrl,
                          XdsFooResourceType::Get()->type_url()),
          1)),
      ::testing::ElementsAre(), ::testing::_));
  EXPECT_THAT(
      GetResourceCounts(),
      ::testing::ElementsAre(::testing::Pair(
          ResourceCountLabelsEq(XdsClient::kOldStyleAuthority,
                                XdsFooResourceType::Get()->type_url(), "acked"),
          1)));
  // Check CSDS data.
  ClientConfig csds = DumpCsds();
  EXPECT_THAT(csds.generic_xds_configs(),
              ::testing::UnorderedElementsAre(CsdsResourceAcked(
                  XdsFooResourceType::Get()->type_url(), "foo1",
                  resource->AsJsonString(), "1", TimestampProtoEq(kTime0))));
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  // Send an update containing an invalid resource.
  // We increment time to make sure that the CSDS data gets a new timestamp.
  time_cache_.TestOnlySetNow(kTime1);
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("2")
          .set_nonce("B")
          .AddInvalidResource(XdsFooResourceType::Get()->type_url(),
                              "{\"name\":\"foo1\",\"value\":[]}")
          .Serialize());
  // XdsClient should deliver an error to the watcher.
  auto error = watcher->WaitForNextAmbientError();
  ASSERT_TRUE(error.has_value());
  EXPECT_EQ(error->code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(error->message(),
            "invalid resource: errors validating JSON: "
            "[field:value error:is not a number] (node ID:xds_client_test)")
      << *error;
  // Check metric data.
  EXPECT_TRUE(metrics_reporter_->WaitForMetricsReporterData(
      ::testing::ElementsAre(::testing::Pair(
          ::testing::Pair(kDefaultXdsServerUrl,
                          XdsFooResourceType::Get()->type_url()),
          1)),
      ::testing::ElementsAre(::testing::Pair(
          ::testing::Pair(kDefaultXdsServerUrl,
                          XdsFooResourceType::Get()->type_url()),
          1)),
      ::testing::_));
  EXPECT_THAT(GetResourceCounts(),
              ::testing::ElementsAre(::testing::Pair(
                  ResourceCountLabelsEq(XdsClient::kOldStyleAuthority,
                                        XdsFooResourceType::Get()->type_url(),
                                        "nacked_but_cached"),
                  1)));
  // Check CSDS data.
  csds = DumpCsds();
  EXPECT_THAT(csds.generic_xds_configs(),
              ::testing::UnorderedElementsAre(CsdsResourceEq(
                  ClientResourceStatus::NACKED,
                  XdsFooResourceType::Get()->type_url(), "foo1",
                  CsdsResourceFields(resource->AsJsonString(), "1",
                                     TimestampProtoEq(kTime0)),
                  CsdsErrorFields("invalid resource: errors validating JSON: "
                                  "[field:value error:is not a number]",
                                  "2", TimestampProtoEq(kTime1)))));
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
  // Start a second watcher for the same resource.  The watcher should
  // first get the cached resource and then the ambient error.
  auto watcher2 = StartFooWatch("foo1");
  resource = watcher2->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  error = watcher2->WaitForNextAmbientError();
  ASSERT_TRUE(error.has_value());
  EXPECT_EQ(
      *error,
      absl::InvalidArgumentError(
          "invalid resource: errors validating JSON: "
          "[field:value error:is not a number] (node ID:xds_client_test)"));
  // Cancel watches.
  CancelFooWatch(watcher.get(), "foo1");
  CancelFooWatch(watcher2.get(), "foo1");
  EXPECT_TRUE(stream->IsOrphaned());
}

TEST_F(XdsClientTest,
       ResourceValidationFailureForCachedResourceWithFailOnDataErrors) {
  testing::ScopedExperimentalEnvVar env_var(
      "GRPC_EXPERIMENTAL_XDS_DATA_ERROR_HANDLING");
  InitXdsClient(FakeXdsBootstrap::Builder().SetServers(
      {FakeXdsBootstrap::FakeXdsServer(kDefaultXdsServerUrl, true)}));
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
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  // Check metric data.
  EXPECT_TRUE(metrics_reporter_->WaitForMetricsReporterData(
      ::testing::ElementsAre(::testing::Pair(
          ::testing::Pair(kDefaultXdsServerUrl,
                          XdsFooResourceType::Get()->type_url()),
          1)),
      ::testing::ElementsAre(), ::testing::_));
  EXPECT_THAT(
      GetResourceCounts(),
      ::testing::ElementsAre(::testing::Pair(
          ResourceCountLabelsEq(XdsClient::kOldStyleAuthority,
                                XdsFooResourceType::Get()->type_url(), "acked"),
          1)));
  // Check CSDS data.
  ClientConfig csds = DumpCsds();
  EXPECT_THAT(csds.generic_xds_configs(),
              ::testing::UnorderedElementsAre(CsdsResourceAcked(
                  XdsFooResourceType::Get()->type_url(), "foo1",
                  resource->AsJsonString(), "1", TimestampProtoEq(kTime0))));
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  // Send an update containing an invalid resource.
  // We increment time to make sure that the CSDS data gets a new timestamp.
  time_cache_.TestOnlySetNow(kTime1);
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
  EXPECT_EQ(error->code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(error->message(),
            "invalid resource: errors validating JSON: "
            "[field:value error:is not a number] (node ID:xds_client_test)")
      << *error;
  // Check metric data.
  EXPECT_TRUE(metrics_reporter_->WaitForMetricsReporterData(
      ::testing::ElementsAre(::testing::Pair(
          ::testing::Pair(kDefaultXdsServerUrl,
                          XdsFooResourceType::Get()->type_url()),
          1)),
      ::testing::ElementsAre(::testing::Pair(
          ::testing::Pair(kDefaultXdsServerUrl,
                          XdsFooResourceType::Get()->type_url()),
          1)),
      ::testing::_));
  EXPECT_THAT(GetResourceCounts(),
              ::testing::ElementsAre(::testing::Pair(
                  ResourceCountLabelsEq(XdsClient::kOldStyleAuthority,
                                        XdsFooResourceType::Get()->type_url(),
                                        "nacked"),
                  1)));
  // CSDS should show that the resource has been requested.
  csds = DumpCsds();
  EXPECT_THAT(
      csds.generic_xds_configs(),
      ::testing::ElementsAre(CsdsResourceEq(
          ClientResourceStatus::NACKED, XdsFooResourceType::Get()->type_url(),
          "foo1", CsdsNoResourceFields(),
          CsdsErrorFields("invalid resource: errors validating JSON: "
                          "[field:value error:is not a number]",
                          "2", TimestampProtoEq(kTime1)))));
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
  // Start a second watcher for the same resource.  This should deliver
  // the error to the watcher immediately.
  auto watcher2 = StartFooWatch("foo1");
  error = watcher2->WaitForNextError();
  ASSERT_TRUE(error.has_value());
  EXPECT_EQ(error->code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(error->message(),
            "invalid resource: errors validating JSON: "
            "[field:value error:is not a number] (node ID:xds_client_test)")
      << *error;
  // Cancel watches.
  CancelFooWatch(watcher.get(), "foo1");
  CancelFooWatch(watcher2.get(), "foo1");
  EXPECT_TRUE(stream->IsOrphaned());
}

TEST_F(XdsClientTest,
       ResourceValidationFailureForCachedResourceWithFailOnDataErrorsDisabled) {
  InitXdsClient(FakeXdsBootstrap::Builder().SetServers(
      {FakeXdsBootstrap::FakeXdsServer(kDefaultXdsServerUrl, true)}));
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
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  // Check metric data.
  EXPECT_TRUE(metrics_reporter_->WaitForMetricsReporterData(
      ::testing::ElementsAre(::testing::Pair(
          ::testing::Pair(kDefaultXdsServerUrl,
                          XdsFooResourceType::Get()->type_url()),
          1)),
      ::testing::ElementsAre(), ::testing::_));
  EXPECT_THAT(
      GetResourceCounts(),
      ::testing::ElementsAre(::testing::Pair(
          ResourceCountLabelsEq(XdsClient::kOldStyleAuthority,
                                XdsFooResourceType::Get()->type_url(), "acked"),
          1)));
  // Check CSDS data.
  ClientConfig csds = DumpCsds();
  EXPECT_THAT(csds.generic_xds_configs(),
              ::testing::UnorderedElementsAre(CsdsResourceAcked(
                  XdsFooResourceType::Get()->type_url(), "foo1",
                  resource->AsJsonString(), "1", TimestampProtoEq(kTime0))));
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  // Send an update containing an invalid resource.
  // We increment time to make sure that the CSDS data gets a new timestamp.
  time_cache_.TestOnlySetNow(kTime1);
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("2")
          .set_nonce("B")
          .AddInvalidResource(XdsFooResourceType::Get()->type_url(),
                              "{\"name\":\"foo1\",\"value\":[]}")
          .Serialize());
  // XdsClient should deliver an ambient error to the watcher.
  auto error = watcher->WaitForNextAmbientError();
  ASSERT_TRUE(error.has_value());
  EXPECT_EQ(error->code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(error->message(),
            "invalid resource: errors validating JSON: "
            "[field:value error:is not a number] (node ID:xds_client_test)")
      << *error;
  // Check metric data.
  EXPECT_TRUE(metrics_reporter_->WaitForMetricsReporterData(
      ::testing::ElementsAre(::testing::Pair(
          ::testing::Pair(kDefaultXdsServerUrl,
                          XdsFooResourceType::Get()->type_url()),
          1)),
      ::testing::ElementsAre(::testing::Pair(
          ::testing::Pair(kDefaultXdsServerUrl,
                          XdsFooResourceType::Get()->type_url()),
          1)),
      ::testing::_));
  EXPECT_THAT(GetResourceCounts(),
              ::testing::ElementsAre(::testing::Pair(
                  ResourceCountLabelsEq(XdsClient::kOldStyleAuthority,
                                        XdsFooResourceType::Get()->type_url(),
                                        "nacked_but_cached"),
                  1)));
  // Check CSDS data.
  csds = DumpCsds();
  EXPECT_THAT(csds.generic_xds_configs(),
              ::testing::UnorderedElementsAre(CsdsResourceEq(
                  ClientResourceStatus::NACKED,
                  XdsFooResourceType::Get()->type_url(), "foo1",
                  CsdsResourceFields(resource->AsJsonString(), "1",
                                     TimestampProtoEq(kTime0)),
                  CsdsErrorFields("invalid resource: errors validating JSON: "
                                  "[field:value error:is not a number]",
                                  "2", TimestampProtoEq(kTime1)))));
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
  // Start a second watcher for the same resource.  The watcher should
  // first get the cached resource and then the ambient error.
  auto watcher2 = StartFooWatch("foo1");
  resource = watcher2->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  error = watcher2->WaitForNextAmbientError();
  ASSERT_TRUE(error.has_value());
  EXPECT_EQ(
      *error,
      absl::InvalidArgumentError(
          "invalid resource: errors validating JSON: "
          "[field:value error:is not a number] (node ID:xds_client_test)"));
  // Cancel watches.
  CancelFooWatch(watcher.get(), "foo1");
  CancelFooWatch(watcher2.get(), "foo1");
  EXPECT_TRUE(stream->IsOrphaned());
}

TEST_F(XdsClientTest, ResourceErrorFromServer) {
  testing::ScopedExperimentalEnvVar env_var(
      "GRPC_EXPERIMENTAL_XDS_DATA_ERROR_HANDLING");
  InitXdsClient();
  // Metrics should initially be empty.
  EXPECT_THAT(metrics_reporter_->resource_updates_valid(),
              ::testing::ElementsAre());
  EXPECT_THAT(metrics_reporter_->resource_updates_invalid(),
              ::testing::ElementsAre());
  EXPECT_THAT(GetResourceCounts(), ::testing::ElementsAre());
  // Start a watch for "foo1".
  auto watcher = StartFooWatch("foo1");
  // Check metrics.
  EXPECT_THAT(metrics_reporter_->resource_updates_valid(),
              ::testing::ElementsAre());
  EXPECT_THAT(metrics_reporter_->resource_updates_invalid(),
              ::testing::ElementsAre());
  EXPECT_THAT(GetResourceCounts(),
              ::testing::ElementsAre(::testing::Pair(
                  ResourceCountLabelsEq(XdsClient::kOldStyleAuthority,
                                        XdsFooResourceType::Get()->type_url(),
                                        "requested"),
                  1)));
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
  // Send a response with an error for the resource.
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("1")
          .set_nonce("A")
          .AddResourceError("foo1", absl::PermissionDeniedError("nope"))
          .Serialize());
  // XdsClient should have delivered the error to the watcher.
  auto error = watcher->WaitForNextError();
  ASSERT_TRUE(error.has_value());
  EXPECT_EQ(*error,
            absl::PermissionDeniedError("nope (node ID:xds_client_test)"));
  // Check metric data.
  EXPECT_TRUE(metrics_reporter_->WaitForMetricsReporterData(
      ::testing::ElementsAre(),
      ::testing::ElementsAre(::testing::Pair(
          ::testing::Pair(kDefaultXdsServerUrl,
                          XdsFooResourceType::Get()->type_url()),
          1)),
      ::testing::_));
  EXPECT_THAT(GetResourceCounts(),
              ::testing::ElementsAre(::testing::Pair(
                  ResourceCountLabelsEq(XdsClient::kOldStyleAuthority,
                                        XdsFooResourceType::Get()->type_url(),
                                        "received_error"),
                  1)));
  // Check CSDS data.
  ClientConfig csds = DumpCsds();
  EXPECT_THAT(
      csds.generic_xds_configs(),
      ::testing::UnorderedElementsAre(CsdsResourceEq(
          ClientResourceStatus::RECEIVED_ERROR,
          XdsFooResourceType::Get()->type_url(), "foo1", CsdsNoResourceFields(),
          CsdsErrorFields("nope", "1", TimestampProtoEq(kTime0)))));
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  // Now server sends a valid resource.
  // We increment time to make sure that the CSDS data gets a new timestamp.
  time_cache_.TestOnlySetNow(kTime1);
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("2")
          .set_nonce("B")
          .AddFooResource(XdsFooResource("foo1", 6))
          .Serialize());
  // XdsClient should have delivered the response to the watcher.
  auto resource = watcher->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  // Check metric data.
  EXPECT_TRUE(metrics_reporter_->WaitForMetricsReporterData(
      ::testing::ElementsAre(::testing::Pair(
          ::testing::Pair(kDefaultXdsServerUrl,
                          XdsFooResourceType::Get()->type_url()),
          1)),
      ::testing::ElementsAre(::testing::Pair(
          ::testing::Pair(kDefaultXdsServerUrl,
                          XdsFooResourceType::Get()->type_url()),
          1)),
      ::testing::_));
  EXPECT_THAT(
      GetResourceCounts(),
      ::testing::ElementsAre(::testing::Pair(
          ResourceCountLabelsEq(XdsClient::kOldStyleAuthority,
                                XdsFooResourceType::Get()->type_url(), "acked"),
          1)));
  // Check CSDS data.
  csds = DumpCsds();
  EXPECT_THAT(csds.generic_xds_configs(),
              ::testing::UnorderedElementsAre(CsdsResourceAcked(
                  XdsFooResourceType::Get()->type_url(), "foo1",
                  resource->AsJsonString(), "2", TimestampProtoEq(kTime1))));
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"2", /*response_nonce=*/"B",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  // Now server sends an error again.
  // We increment time to make sure that the CSDS data gets a new timestamp.
  time_cache_.TestOnlySetNow(kTime2);
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("3")
          .set_nonce("C")
          .AddResourceError("foo1", absl::PermissionDeniedError("bzzt"))
          .Serialize());
  // XdsClient should have delivered an ambient error to the watcher.
  error = watcher->WaitForNextAmbientError();
  ASSERT_TRUE(error.has_value());
  EXPECT_EQ(*error,
            absl::PermissionDeniedError("bzzt (node ID:xds_client_test)"));
  // Check metric data.
  EXPECT_TRUE(metrics_reporter_->WaitForMetricsReporterData(
      ::testing::ElementsAre(::testing::Pair(
          ::testing::Pair(kDefaultXdsServerUrl,
                          XdsFooResourceType::Get()->type_url()),
          1)),
      ::testing::ElementsAre(::testing::Pair(
          ::testing::Pair(kDefaultXdsServerUrl,
                          XdsFooResourceType::Get()->type_url()),
          2)),
      ::testing::_));
  EXPECT_THAT(GetResourceCounts(),
              ::testing::ElementsAre(::testing::Pair(
                  ResourceCountLabelsEq(XdsClient::kOldStyleAuthority,
                                        XdsFooResourceType::Get()->type_url(),
                                        "received_error_but_cached"),
                  1)));
  // Check CSDS data.
  csds = DumpCsds();
  EXPECT_THAT(csds.generic_xds_configs(),
              ::testing::UnorderedElementsAre(CsdsResourceEq(
                  ClientResourceStatus::RECEIVED_ERROR,
                  XdsFooResourceType::Get()->type_url(), "foo1",
                  CsdsResourceFields(resource->AsJsonString(), "2",
                                     TimestampProtoEq(kTime1)),
                  CsdsErrorFields("bzzt", "3", TimestampProtoEq(kTime2)))));
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"3", /*response_nonce=*/"C",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  // Cancel watch.
  CancelFooWatch(watcher.get(), "foo1");
  EXPECT_TRUE(stream->IsOrphaned());
  // Check metric data.
  EXPECT_TRUE(metrics_reporter_->WaitForMetricsReporterData(
      ::testing::ElementsAre(::testing::Pair(
          ::testing::Pair(kDefaultXdsServerUrl,
                          XdsFooResourceType::Get()->type_url()),
          1)),
      ::testing::ElementsAre(::testing::Pair(
          ::testing::Pair(kDefaultXdsServerUrl,
                          XdsFooResourceType::Get()->type_url()),
          2)),
      ::testing::_));
  EXPECT_THAT(GetResourceCounts(), ::testing::ElementsAre());
}

TEST_F(XdsClientTest, ResourceErrorFromServerWithFailOnDataErrors) {
  testing::ScopedExperimentalEnvVar env_var(
      "GRPC_EXPERIMENTAL_XDS_DATA_ERROR_HANDLING");
  InitXdsClient(FakeXdsBootstrap::Builder().SetServers(
      {FakeXdsBootstrap::FakeXdsServer(kDefaultXdsServerUrl, true)}));
  // Metrics should initially be empty.
  EXPECT_THAT(metrics_reporter_->resource_updates_valid(),
              ::testing::ElementsAre());
  EXPECT_THAT(metrics_reporter_->resource_updates_invalid(),
              ::testing::ElementsAre());
  EXPECT_THAT(GetResourceCounts(), ::testing::ElementsAre());
  // Start a watch for "foo1".
  auto watcher = StartFooWatch("foo1");
  // Check metrics.
  EXPECT_THAT(metrics_reporter_->resource_updates_valid(),
              ::testing::ElementsAre());
  EXPECT_THAT(metrics_reporter_->resource_updates_invalid(),
              ::testing::ElementsAre());
  EXPECT_THAT(GetResourceCounts(),
              ::testing::ElementsAre(::testing::Pair(
                  ResourceCountLabelsEq(XdsClient::kOldStyleAuthority,
                                        XdsFooResourceType::Get()->type_url(),
                                        "requested"),
                  1)));
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
  // Send a response with an error for the resource.
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("1")
          .set_nonce("A")
          .AddResourceError("foo1", absl::PermissionDeniedError("nope"))
          .Serialize());
  // XdsClient should have delivered the error to the watcher.
  auto error = watcher->WaitForNextError();
  ASSERT_TRUE(error.has_value());
  EXPECT_EQ(*error,
            absl::PermissionDeniedError("nope (node ID:xds_client_test)"));
  // Check metric data.
  EXPECT_TRUE(metrics_reporter_->WaitForMetricsReporterData(
      ::testing::ElementsAre(),
      ::testing::ElementsAre(::testing::Pair(
          ::testing::Pair(kDefaultXdsServerUrl,
                          XdsFooResourceType::Get()->type_url()),
          1)),
      ::testing::_));
  EXPECT_THAT(GetResourceCounts(),
              ::testing::ElementsAre(::testing::Pair(
                  ResourceCountLabelsEq(XdsClient::kOldStyleAuthority,
                                        XdsFooResourceType::Get()->type_url(),
                                        "received_error"),
                  1)));
  // Check CSDS data.
  ClientConfig csds = DumpCsds();
  EXPECT_THAT(
      csds.generic_xds_configs(),
      ::testing::UnorderedElementsAre(CsdsResourceEq(
          ClientResourceStatus::RECEIVED_ERROR,
          XdsFooResourceType::Get()->type_url(), "foo1", CsdsNoResourceFields(),
          CsdsErrorFields("nope", "1", TimestampProtoEq(kTime0)))));
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  // Now server sends a valid resource.
  // We increment time to make sure that the CSDS data gets a new timestamp.
  time_cache_.TestOnlySetNow(kTime1);
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("2")
          .set_nonce("B")
          .AddFooResource(XdsFooResource("foo1", 6))
          .Serialize());
  // XdsClient should have delivered the response to the watcher.
  auto resource = watcher->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  // Check metric data.
  EXPECT_TRUE(metrics_reporter_->WaitForMetricsReporterData(
      ::testing::ElementsAre(::testing::Pair(
          ::testing::Pair(kDefaultXdsServerUrl,
                          XdsFooResourceType::Get()->type_url()),
          1)),
      ::testing::ElementsAre(::testing::Pair(
          ::testing::Pair(kDefaultXdsServerUrl,
                          XdsFooResourceType::Get()->type_url()),
          1)),
      ::testing::_));
  EXPECT_THAT(
      GetResourceCounts(),
      ::testing::ElementsAre(::testing::Pair(
          ResourceCountLabelsEq(XdsClient::kOldStyleAuthority,
                                XdsFooResourceType::Get()->type_url(), "acked"),
          1)));
  // Check CSDS data.
  csds = DumpCsds();
  EXPECT_THAT(csds.generic_xds_configs(),
              ::testing::UnorderedElementsAre(CsdsResourceAcked(
                  XdsFooResourceType::Get()->type_url(), "foo1",
                  resource->AsJsonString(), "2", TimestampProtoEq(kTime1))));
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"2", /*response_nonce=*/"B",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  // Now server sends an error again.
  // We increment time to make sure that the CSDS data gets a new timestamp.
  time_cache_.TestOnlySetNow(kTime2);
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("3")
          .set_nonce("C")
          .AddResourceError("foo1", absl::PermissionDeniedError("bzzt"))
          .Serialize());
  // XdsClient should have delivered a (non-ambient) error to the watcher.
  error = watcher->WaitForNextError();
  ASSERT_TRUE(error.has_value());
  EXPECT_EQ(*error,
            absl::PermissionDeniedError("bzzt (node ID:xds_client_test)"));
  // Check metric data.
  EXPECT_TRUE(metrics_reporter_->WaitForMetricsReporterData(
      ::testing::ElementsAre(::testing::Pair(
          ::testing::Pair(kDefaultXdsServerUrl,
                          XdsFooResourceType::Get()->type_url()),
          1)),
      ::testing::ElementsAre(::testing::Pair(
          ::testing::Pair(kDefaultXdsServerUrl,
                          XdsFooResourceType::Get()->type_url()),
          2)),
      ::testing::_));
  EXPECT_THAT(GetResourceCounts(),
              ::testing::ElementsAre(::testing::Pair(
                  ResourceCountLabelsEq(XdsClient::kOldStyleAuthority,
                                        XdsFooResourceType::Get()->type_url(),
                                        "received_error"),
                  1)));
  // Check CSDS data.
  csds = DumpCsds();
  EXPECT_THAT(
      csds.generic_xds_configs(),
      ::testing::UnorderedElementsAre(CsdsResourceEq(
          ClientResourceStatus::RECEIVED_ERROR,
          XdsFooResourceType::Get()->type_url(), "foo1", CsdsNoResourceFields(),
          CsdsErrorFields("bzzt", "3", TimestampProtoEq(kTime2)))));
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"3", /*response_nonce=*/"C",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  // Cancel watch.
  CancelFooWatch(watcher.get(), "foo1");
  EXPECT_TRUE(stream->IsOrphaned());
  // Check metric data.
  EXPECT_TRUE(metrics_reporter_->WaitForMetricsReporterData(
      ::testing::ElementsAre(::testing::Pair(
          ::testing::Pair(kDefaultXdsServerUrl,
                          XdsFooResourceType::Get()->type_url()),
          1)),
      ::testing::ElementsAre(::testing::Pair(
          ::testing::Pair(kDefaultXdsServerUrl,
                          XdsFooResourceType::Get()->type_url()),
          2)),
      ::testing::_));
  EXPECT_THAT(GetResourceCounts(), ::testing::ElementsAre());
}

TEST_F(XdsClientTest, ResourceErrorFromServerIgnoredIfNotEnabled) {
  InitXdsClient();
  // Metrics should initially be empty.
  EXPECT_THAT(metrics_reporter_->resource_updates_valid(),
              ::testing::ElementsAre());
  EXPECT_THAT(metrics_reporter_->resource_updates_invalid(),
              ::testing::ElementsAre());
  EXPECT_THAT(GetResourceCounts(), ::testing::ElementsAre());
  // Start a watch for "foo1".
  auto watcher = StartFooWatch("foo1");
  // Check metrics.
  EXPECT_THAT(metrics_reporter_->resource_updates_valid(),
              ::testing::ElementsAre());
  EXPECT_THAT(metrics_reporter_->resource_updates_invalid(),
              ::testing::ElementsAre());
  EXPECT_THAT(GetResourceCounts(),
              ::testing::ElementsAre(::testing::Pair(
                  ResourceCountLabelsEq(XdsClient::kOldStyleAuthority,
                                        XdsFooResourceType::Get()->type_url(),
                                        "requested"),
                  1)));
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
  // Send a response with an error for the resource.
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("1")
          .set_nonce("A")
          .AddResourceError("foo1", absl::PermissionDeniedError("nope"))
          .Serialize());
  // XdsClient will ignore the error, so watcher should not see any event.
  EXPECT_FALSE(watcher->HasEvent());
  // Check metric data.
  EXPECT_TRUE(metrics_reporter_->WaitForMetricsReporterData(
      ::testing::ElementsAre(), ::testing::ElementsAre(), ::testing::_));
  EXPECT_THAT(GetResourceCounts(),
              ::testing::ElementsAre(::testing::Pair(
                  ResourceCountLabelsEq(XdsClient::kOldStyleAuthority,
                                        XdsFooResourceType::Get()->type_url(),
                                        "requested"),
                  1)));
  // CSDS should show that the resource has been requested.
  ClientConfig csds = DumpCsds();
  EXPECT_THAT(csds.generic_xds_configs(),
              ::testing::ElementsAre(CsdsResourceRequested(
                  XdsFooResourceType::Get()->type_url(), "foo1")));
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  // Now server sends a valid resource.
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("2")
          .set_nonce("B")
          .AddFooResource(XdsFooResource("foo1", 6))
          .Serialize());
  // XdsClient should have delivered the response to the watcher.
  auto resource = watcher->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  // Check metric data.
  EXPECT_TRUE(metrics_reporter_->WaitForMetricsReporterData(
      ::testing::ElementsAre(::testing::Pair(
          ::testing::Pair(kDefaultXdsServerUrl,
                          XdsFooResourceType::Get()->type_url()),
          1)),
      ::testing::ElementsAre(), ::testing::_));
  EXPECT_THAT(
      GetResourceCounts(),
      ::testing::ElementsAre(::testing::Pair(
          ResourceCountLabelsEq(XdsClient::kOldStyleAuthority,
                                XdsFooResourceType::Get()->type_url(), "acked"),
          1)));
  // Check CSDS data.
  csds = DumpCsds();
  EXPECT_THAT(csds.generic_xds_configs(),
              ::testing::UnorderedElementsAre(CsdsResourceAcked(
                  XdsFooResourceType::Get()->type_url(), "foo1",
                  resource->AsJsonString(), "2", TimestampProtoEq(kTime0))));
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"2", /*response_nonce=*/"B",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  // Cancel watch.
  CancelFooWatch(watcher.get(), "foo1");
  EXPECT_TRUE(stream->IsOrphaned());
  // Check metric data.
  EXPECT_TRUE(metrics_reporter_->WaitForMetricsReporterData(
      ::testing::ElementsAre(::testing::Pair(
          ::testing::Pair(kDefaultXdsServerUrl,
                          XdsFooResourceType::Get()->type_url()),
          1)),
      ::testing::ElementsAre(), ::testing::_));
  EXPECT_THAT(GetResourceCounts(), ::testing::ElementsAre());
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
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "wc1");
  EXPECT_EQ(resource->value, 6);
  // Check metric data.
  EXPECT_TRUE(metrics_reporter_->WaitForMetricsReporterData(
      ::testing::ElementsAre(::testing::Pair(
          ::testing::Pair(kDefaultXdsServerUrl,
                          XdsWildcardCapableResourceType::Get()->type_url()),
          1)),
      ::testing::ElementsAre(::testing::Pair(
          ::testing::Pair(kDefaultXdsServerUrl,
                          XdsWildcardCapableResourceType::Get()->type_url()),
          1)),
      ::testing::_));
  EXPECT_THAT(
      GetResourceCounts(),
      ::testing::ElementsAre(::testing::Pair(
          ResourceCountLabelsEq(
              XdsClient::kOldStyleAuthority,
              XdsWildcardCapableResourceType::Get()->type_url(), "acked"),
          1)));
  // Check CSDS data.
  ClientConfig csds = DumpCsds();
  EXPECT_THAT(csds.generic_xds_configs(),
              ::testing::UnorderedElementsAre(CsdsResourceAcked(
                  XdsWildcardCapableResourceType::Get()->type_url(), "wc1",
                  resource->AsJsonString(), "1", TimestampProtoEq(kTime0))));
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
  EXPECT_TRUE(stream->IsOrphaned());
}

// This tests resource removal triggered by the server when using a
// resource type that requires all resources to be present in every
// response, similar to LDS and CDS.  It configures the
// fail_on_data_errors server feature.
TEST_F(XdsClientTest, ResourceDeletionWithFailOnDataErrors) {
  InitXdsClient(FakeXdsBootstrap::Builder().SetServers(
      {FakeXdsBootstrap::FakeXdsServer(kDefaultXdsServerUrl, true)}));
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
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "wc1");
  EXPECT_EQ(resource->value, 6);
  // Check metric data.
  EXPECT_TRUE(metrics_reporter_->WaitForMetricsReporterData(
      ::testing::ElementsAre(::testing::Pair(
          ::testing::Pair(kDefaultXdsServerUrl,
                          XdsWildcardCapableResourceType::Get()->type_url()),
          1)),
      ::testing::ElementsAre(), ::testing::_));
  EXPECT_THAT(
      GetResourceCounts(),
      ::testing::ElementsAre(::testing::Pair(
          ResourceCountLabelsEq(
              XdsClient::kOldStyleAuthority,
              XdsWildcardCapableResourceType::Get()->type_url(), "acked"),
          1)));
  // Check CSDS data.
  ClientConfig csds = DumpCsds();
  EXPECT_THAT(csds.generic_xds_configs(),
              ::testing::UnorderedElementsAre(CsdsResourceAcked(
                  XdsWildcardCapableResourceType::Get()->type_url(), "wc1",
                  resource->AsJsonString(), "1", TimestampProtoEq(kTime0))));
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsWildcardCapableResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"wc1"});
  // Server now sends a response without the resource, thus indicating
  // it's been deleted.
  // We increment time to make sure that the CSDS data gets a new timestamp.
  time_cache_.TestOnlySetNow(kTime1);
  stream->SendMessageToClient(
      ResponseBuilder(XdsWildcardCapableResourceType::Get()->type_url())
          .set_version_info("2")
          .set_nonce("B")
          .Serialize());
  // Watcher should see the does-not-exist event.
  EXPECT_TRUE(watcher->WaitForDoesNotExist());
  // Check metric data.
  EXPECT_TRUE(metrics_reporter_->WaitForMetricsReporterData(
      ::testing::ElementsAre(::testing::Pair(
          ::testing::Pair(kDefaultXdsServerUrl,
                          XdsWildcardCapableResourceType::Get()->type_url()),
          1)),
      ::testing::ElementsAre(), ::testing::_));
  EXPECT_THAT(GetResourceCounts(),
              ::testing::ElementsAre(::testing::Pair(
                  ResourceCountLabelsEq(
                      XdsClient::kOldStyleAuthority,
                      XdsWildcardCapableResourceType::Get()->type_url(),
                      "does_not_exist"),
                  1)));
  // Check CSDS data.
  csds = DumpCsds();
  EXPECT_THAT(
      csds.generic_xds_configs(),
      ::testing::UnorderedElementsAre(CsdsResourceEq(
          ClientResourceStatus::DOES_NOT_EXIST,
          XdsWildcardCapableResourceType::Get()->type_url(), "wc1",
          CsdsNoResourceFields(),
          CsdsErrorFields("does not exist", "2", TimestampProtoEq(kTime1)))));
  // Start a new watcher for the same resource.  It should immediately
  // receive the same does-not-exist notification.
  auto watcher2 = StartWildcardCapableWatch("wc1");
  EXPECT_TRUE(watcher2->WaitForDoesNotExist());
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsWildcardCapableResourceType::Get()->type_url(),
               /*version_info=*/"2", /*response_nonce=*/"B",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"wc1"});
  // Server sends the resource again.
  // We increment time to make sure that the CSDS data gets a new timestamp.
  time_cache_.TestOnlySetNow(kTime2);
  stream->SendMessageToClient(
      ResponseBuilder(XdsWildcardCapableResourceType::Get()->type_url())
          .set_version_info("3")
          .set_nonce("C")
          .AddWildcardCapableResource(XdsWildcardCapableResource("wc1", 7))
          .Serialize());
  // XdsClient should have delivered the response to the watchers.
  resource = watcher->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "wc1");
  EXPECT_EQ(resource->value, 7);
  resource = watcher2->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "wc1");
  EXPECT_EQ(resource->value, 7);
  // Check metric data.
  EXPECT_TRUE(metrics_reporter_->WaitForMetricsReporterData(
      ::testing::ElementsAre(::testing::Pair(
          ::testing::Pair(kDefaultXdsServerUrl,
                          XdsWildcardCapableResourceType::Get()->type_url()),
          2)),
      ::testing::ElementsAre(), ::testing::_));
  EXPECT_THAT(
      GetResourceCounts(),
      ::testing::ElementsAre(::testing::Pair(
          ResourceCountLabelsEq(
              XdsClient::kOldStyleAuthority,
              XdsWildcardCapableResourceType::Get()->type_url(), "acked"),
          1)));
  // Check CSDS data.
  csds = DumpCsds();
  EXPECT_THAT(csds.generic_xds_configs(),
              ::testing::UnorderedElementsAre(CsdsResourceAcked(
                  XdsWildcardCapableResourceType::Get()->type_url(), "wc1",
                  resource->AsJsonString(), "3", TimestampProtoEq(kTime2))));
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
  EXPECT_TRUE(stream->IsOrphaned());
}

// This tests that when we ignore resource deletions from the server by
// default.
TEST_F(XdsClientTest, ResourceDeletionIgnoredByDefault) {
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
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "wc1");
  EXPECT_EQ(resource->value, 6);
  // Check metric data.
  EXPECT_TRUE(metrics_reporter_->WaitForMetricsReporterData(
      ::testing::ElementsAre(::testing::Pair(
          ::testing::Pair(kDefaultXdsServerUrl,
                          XdsWildcardCapableResourceType::Get()->type_url()),
          1)),
      ::testing::ElementsAre(), ::testing::_));
  EXPECT_THAT(
      GetResourceCounts(),
      ::testing::ElementsAre(::testing::Pair(
          ResourceCountLabelsEq(
              XdsClient::kOldStyleAuthority,
              XdsWildcardCapableResourceType::Get()->type_url(), "acked"),
          1)));
  // Check CSDS data.
  ClientConfig csds = DumpCsds();
  EXPECT_THAT(csds.generic_xds_configs(),
              ::testing::UnorderedElementsAre(CsdsResourceAcked(
                  XdsWildcardCapableResourceType::Get()->type_url(), "wc1",
                  resource->AsJsonString(), "1", TimestampProtoEq(kTime0))));
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsWildcardCapableResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"wc1"});
  // Server now sends a response without the resource, thus indicating
  // it's been deleted.
  // We increment time to make sure that the CSDS data does NOT get a
  // new timestamp.
  time_cache_.TestOnlySetNow(kTime1);
  stream->SendMessageToClient(
      ResponseBuilder(XdsWildcardCapableResourceType::Get()->type_url())
          .set_version_info("2")
          .set_nonce("B")
          .Serialize());
  // Watcher should see an ambient error, since we should have ignored the
  // deletion.
  auto error = watcher->WaitForNextAmbientError();
  ASSERT_TRUE(error.has_value());
  EXPECT_EQ(*error,
            absl::NotFoundError("does not exist (node ID:xds_client_test)"));
  // Check metric data.
  EXPECT_TRUE(metrics_reporter_->WaitForMetricsReporterData(
      ::testing::ElementsAre(::testing::Pair(
          ::testing::Pair(kDefaultXdsServerUrl,
                          XdsWildcardCapableResourceType::Get()->type_url()),
          1)),
      ::testing::ElementsAre(), ::testing::_));
  EXPECT_THAT(GetResourceCounts(),
              ::testing::ElementsAre(::testing::Pair(
                  ResourceCountLabelsEq(
                      XdsClient::kOldStyleAuthority,
                      XdsWildcardCapableResourceType::Get()->type_url(),
                      "does_not_exist_but_cached"),
                  1)));
  // Check CSDS data.
  csds = DumpCsds();
  EXPECT_THAT(
      csds.generic_xds_configs(),
      ::testing::UnorderedElementsAre(CsdsResourceEq(
          ClientResourceStatus::DOES_NOT_EXIST,
          XdsWildcardCapableResourceType::Get()->type_url(), "wc1",
          CsdsResourceFields(resource->AsJsonString(), "1",
                             TimestampProtoEq(kTime0)),
          CsdsErrorFields("does not exist", "2", TimestampProtoEq(kTime1)))));
  // Start a new watcher for the same resource.  It should immediately
  // receive the cached resource.
  auto watcher2 = StartWildcardCapableWatch("wc1");
  resource = watcher2->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "wc1");
  EXPECT_EQ(resource->value, 6);
  error = watcher2->WaitForNextAmbientError();
  ASSERT_TRUE(error.has_value());
  EXPECT_EQ(*error,
            absl::NotFoundError("does not exist (node ID:xds_client_test)"));
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsWildcardCapableResourceType::Get()->type_url(),
               /*version_info=*/"2", /*response_nonce=*/"B",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"wc1"});
  // Server sends a new value for the resource.
  // We increment time to make sure that the CSDS data gets a new timestamp.
  time_cache_.TestOnlySetNow(kTime2);
  stream->SendMessageToClient(
      ResponseBuilder(XdsWildcardCapableResourceType::Get()->type_url())
          .set_version_info("3")
          .set_nonce("C")
          .AddWildcardCapableResource(XdsWildcardCapableResource("wc1", 7))
          .Serialize());
  // XdsClient should have delivered the response to the watchers.
  resource = watcher->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "wc1");
  EXPECT_EQ(resource->value, 7);
  resource = watcher2->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "wc1");
  EXPECT_EQ(resource->value, 7);
  // Check metric data.
  EXPECT_TRUE(metrics_reporter_->WaitForMetricsReporterData(
      ::testing::ElementsAre(::testing::Pair(
          ::testing::Pair(kDefaultXdsServerUrl,
                          XdsWildcardCapableResourceType::Get()->type_url()),
          2)),
      ::testing::ElementsAre(), ::testing::_));
  EXPECT_THAT(
      GetResourceCounts(),
      ::testing::ElementsAre(::testing::Pair(
          ResourceCountLabelsEq(
              XdsClient::kOldStyleAuthority,
              XdsWildcardCapableResourceType::Get()->type_url(), "acked"),
          1)));
  // Check CSDS data.
  csds = DumpCsds();
  EXPECT_THAT(csds.generic_xds_configs(),
              ::testing::UnorderedElementsAre(CsdsResourceAcked(
                  XdsWildcardCapableResourceType::Get()->type_url(), "wc1",
                  resource->AsJsonString(), "3", TimestampProtoEq(kTime2))));
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
  EXPECT_TRUE(stream->IsOrphaned());
}

TEST_F(XdsClientTest, StreamClosedByServer) {
  InitXdsClient();
  // Metrics should initially be empty.
  EXPECT_THAT(GetServerConnections(), ::testing::ElementsAre());
  // Start a watch for "foo1".
  auto watcher = StartFooWatch("foo1");
  // Check metric data.
  EXPECT_THAT(GetServerConnections(), ::testing::ElementsAre(::testing::Pair(
                                          kDefaultXdsServerUrl, true)));
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
  ASSERT_NE(resource, nullptr);
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
  EXPECT_TRUE(stream->IsOrphaned());
  // Check metric data.
  EXPECT_THAT(GetServerConnections(), ::testing::ElementsAre(::testing::Pair(
                                          kDefaultXdsServerUrl, true)));
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
  ASSERT_NE(resource, nullptr);
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
  EXPECT_TRUE(stream->IsOrphaned());
}

TEST_F(XdsClientTest, StreamClosedByServerWithoutSeeingResponse) {
  InitXdsClient();
  // Metrics should initially be empty.
  EXPECT_THAT(GetServerConnections(), ::testing::ElementsAre());
  // Start a watch for "foo1".
  auto watcher = StartFooWatch("foo1");
  // Check metric data.
  EXPECT_THAT(GetServerConnections(), ::testing::ElementsAre(::testing::Pair(
                                          kDefaultXdsServerUrl, true)));
  EXPECT_THAT(metrics_reporter_->server_failures(), ::testing::ElementsAre());
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
  // Check metric data.
  EXPECT_THAT(GetServerConnections(), ::testing::ElementsAre(::testing::Pair(
                                          kDefaultXdsServerUrl, false)));
  EXPECT_TRUE(metrics_reporter_->WaitForMetricsReporterData(
      ::testing::_, ::testing::_,
      ::testing::ElementsAre(::testing::Pair(kDefaultXdsServerUrl, 1))));
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
  // Connection still reported as unhappy until we get a response.
  EXPECT_THAT(GetServerConnections(), ::testing::ElementsAre(::testing::Pair(
                                          kDefaultXdsServerUrl, false)));
  EXPECT_TRUE(metrics_reporter_->WaitForMetricsReporterData(
      ::testing::_, ::testing::_,
      ::testing::ElementsAre(::testing::Pair(kDefaultXdsServerUrl, 1))));
  // Server now sends the requested resource.
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("1")
          .set_nonce("A")
          .AddFooResource(XdsFooResource("foo1", 6))
          .Serialize());
  // Watcher gets the resource.
  auto resource = watcher->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  // Connection now reported as happy.
  EXPECT_THAT(GetServerConnections(), ::testing::ElementsAre(::testing::Pair(
                                          kDefaultXdsServerUrl, true)));
  // XdsClient sends an ACK.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  // Cancel watcher.
  CancelFooWatch(watcher.get(), "foo1");
  EXPECT_TRUE(stream->IsOrphaned());
}

TEST_F(XdsClientTest, ConnectionFails) {
  InitXdsClient();
  // Tell transport to let us manually trigger completion of the
  // send_message ops to XdsClient.
  transport_factory_->SetAutoCompleteMessagesFromClient(false);
  // Metrics should initially be empty.
  EXPECT_THAT(GetServerConnections(), ::testing::ElementsAre());
  EXPECT_THAT(metrics_reporter_->server_failures(), ::testing::ElementsAre());
  // Start a watch for "foo1".
  auto watcher = StartFooWatch("foo1");
  // Check metric data.
  EXPECT_THAT(GetServerConnections(), ::testing::ElementsAre(::testing::Pair(
                                          kDefaultXdsServerUrl, true)));
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
  TriggerConnectionFailure(*xds_client_->bootstrap().servers().front(),
                           absl::UnavailableError("connection failed"));
  // XdsClient should report an error to the watcher.
  auto error = watcher->WaitForNextError();
  ASSERT_TRUE(error.has_value());
  EXPECT_EQ(error->code(), absl::StatusCode::kUnavailable);
  EXPECT_EQ(error->message(),
            "xDS channel for server default_xds_server: "
            "connection failed (node ID:xds_client_test)")
      << *error;
  // Connection reported as unhappy.
  EXPECT_THAT(GetServerConnections(), ::testing::ElementsAre(::testing::Pair(
                                          kDefaultXdsServerUrl, false)));
  EXPECT_TRUE(metrics_reporter_->WaitForMetricsReporterData(
      ::testing::_, ::testing::_,
      ::testing::ElementsAre(::testing::Pair(kDefaultXdsServerUrl, 1))));
  // We should not see a resource-does-not-exist event, because the
  // timer should not be running while the channel is disconnected.
  EXPECT_TRUE(watcher->ExpectNoEvent());
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
  // Connection now reported as happy.
  EXPECT_THAT(GetServerConnections(), ::testing::ElementsAre(::testing::Pair(
                                          kDefaultXdsServerUrl, true)));
  // XdsClient should have delivered the response to the watchers.
  auto resource = watcher->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  resource = watcher2->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
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
  EXPECT_TRUE(stream->IsOrphaned());
}

TEST_F(XdsClientTest, ConnectionFailsWithCachedResource) {
  InitXdsClient();
  // Start a watch for "foo1".
  auto watcher = StartFooWatch("foo1");
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
  // XdsClient should have delivered the resource to the watcher.
  auto resource = watcher->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  // Transport reports connection failure.
  TriggerConnectionFailure(*xds_client_->bootstrap().servers().front(),
                           absl::UnavailableError("connection failed"));
  // XdsClient should report an ambient error to the watcher.
  auto error = watcher->WaitForNextAmbientError();
  ASSERT_TRUE(error.has_value());
  EXPECT_EQ(error->code(), absl::StatusCode::kUnavailable);
  EXPECT_EQ(error->message(),
            "xDS channel for server default_xds_server: "
            "connection failed (node ID:xds_client_test)")
      << *error;
  // The transport failing should also cause the stream to terminate.
  stream->MaybeSendStatusToClient(absl::UnavailableError("ugh"));
  // The XdsClient will create a new stream.
  stream = WaitForAdsStream();
  ASSERT_TRUE(stream != nullptr);
  // XdsClient should have sent a subscription request on the new stream
  // that includes the last seen version.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  CheckRequestNode(*request);  // Should be present on the first request.
  // Start a new watch.  This watcher should be given the cached resource
  // followed by the ambient error.
  auto watcher2 = StartFooWatch("foo1");
  resource = watcher2->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  error = watcher2->WaitForNextAmbientError();
  ASSERT_TRUE(error.has_value());
  EXPECT_EQ(error->code(), absl::StatusCode::kUnavailable);
  EXPECT_EQ(error->message(),
            "xDS channel for server default_xds_server: "
            "connection failed (node ID:xds_client_test)")
      << *error;
  // The ADS stream uses wait_for_ready inside the XdsTransport interface,
  // so when the channel reconnects, the already-started stream will proceed.
  // Server sends a response with the same resource contents as before.
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("1")
          .set_nonce("A")
          .AddFooResource(XdsFooResource("foo1", 6))
          .Serialize());
  // The resource hasn't changed, so XdsClient will not call the
  // watchers' OnResourceChanged() methods.  However, it will call
  // OnAmbientError() with an OK status to let them know that the
  // ambient error is gone.
  error = watcher->WaitForNextAmbientError();
  ASSERT_TRUE(error.has_value());
  EXPECT_EQ(*error, absl::OkStatus());
  error = watcher2->WaitForNextAmbientError();
  ASSERT_TRUE(error.has_value());
  EXPECT_EQ(*error, absl::OkStatus());
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  // Cancel watches.
  CancelFooWatch(watcher.get(), "foo1");
  CancelFooWatch(watcher2.get(), "foo1");
  EXPECT_TRUE(stream->IsOrphaned());
}

TEST_F(XdsClientTest, ResourceDoesNotExistUponTimeout) {
  event_engine_->SetRunAfterDurationCallback(
      [&](grpc_event_engine::experimental::EventEngine::Duration duration) {
        grpc_event_engine::experimental::EventEngine::Duration expected =
            std::chrono::seconds(15);
        EXPECT_EQ(duration, expected) << "Expected: " << expected.count()
                                      << "\nActual:   " << duration.count();
      });
  InitXdsClient();
  // Start a watch for "foo1".
  auto watcher = StartFooWatch("foo1");
  // Watcher should initially not see any resource reported.
  EXPECT_FALSE(watcher->HasEvent());
  // Check metric data.
  EXPECT_THAT(metrics_reporter_->resource_updates_valid(),
              ::testing::ElementsAre());
  EXPECT_THAT(metrics_reporter_->resource_updates_invalid(),
              ::testing::ElementsAre());
  EXPECT_THAT(GetResourceCounts(),
              ::testing::ElementsAre(::testing::Pair(
                  ResourceCountLabelsEq(XdsClient::kOldStyleAuthority,
                                        XdsFooResourceType::Get()->type_url(),
                                        "requested"),
                  1)));
  // CSDS should show that the resource has been requested.
  ClientConfig csds = DumpCsds();
  EXPECT_THAT(csds.generic_xds_configs(),
              ::testing::ElementsAre(CsdsResourceRequested(
                  XdsFooResourceType::Get()->type_url(), "foo1")));
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
  EXPECT_TRUE(watcher->WaitForDoesNotExist());
  // Check metric data.
  EXPECT_TRUE(metrics_reporter_->WaitForMetricsReporterData(
      ::testing::ElementsAre(), ::testing::ElementsAre(), ::testing::_));
  EXPECT_THAT(GetResourceCounts(),
              ::testing::ElementsAre(::testing::Pair(
                  ResourceCountLabelsEq(XdsClient::kOldStyleAuthority,
                                        XdsFooResourceType::Get()->type_url(),
                                        "does_not_exist"),
                  1)));
  // CSDS should show that the resource has been requested.
  csds = DumpCsds();
  EXPECT_THAT(csds.generic_xds_configs(),
              ::testing::ElementsAre(CsdsResourceDoesNotExistOnTimeout(
                  XdsFooResourceType::Get()->type_url(), "foo1")));
  // Start a new watcher for the same resource.  It should immediately
  // receive the same does-not-exist notification.
  auto watcher2 = StartFooWatch("foo1");
  EXPECT_TRUE(watcher2->WaitForDoesNotExist());
  // Now server sends a response.
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("1")
          .set_nonce("A")
          .AddFooResource(XdsFooResource("foo1", 6))
          .Serialize());
  // XdsClient should have delivered the response to the watchers.
  auto resource = watcher->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  resource = watcher2->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  // Check metric data.
  EXPECT_TRUE(metrics_reporter_->WaitForMetricsReporterData(
      ::testing::ElementsAre(::testing::Pair(
          ::testing::Pair(kDefaultXdsServerUrl,
                          XdsFooResourceType::Get()->type_url()),
          1)),
      ::testing::ElementsAre(), ::testing::_));
  EXPECT_THAT(
      GetResourceCounts(),
      ::testing::ElementsAre(::testing::Pair(
          ResourceCountLabelsEq(XdsClient::kOldStyleAuthority,
                                XdsFooResourceType::Get()->type_url(), "acked"),
          1)));
  // Check CSDS data.
  csds = DumpCsds();
  EXPECT_THAT(csds.generic_xds_configs(),
              ::testing::UnorderedElementsAre(CsdsResourceAcked(
                  XdsFooResourceType::Get()->type_url(), "foo1",
                  resource->AsJsonString(), "1", TimestampProtoEq(kTime0))));
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
  EXPECT_TRUE(stream->IsOrphaned());
}

TEST_F(XdsClientTest, ResourceTimerIsTransientErrorIgnoredUnlessEnabled) {
  event_engine_->SetRunAfterDurationCallback(
      [&](grpc_event_engine::experimental::EventEngine::Duration duration) {
        grpc_event_engine::experimental::EventEngine::Duration expected =
            std::chrono::seconds(15);
        EXPECT_EQ(duration, expected) << "Expected: " << expected.count()
                                      << "\nActual:   " << duration.count();
      });
  InitXdsClient(FakeXdsBootstrap::Builder().SetServers(
      {FakeXdsBootstrap::FakeXdsServer(kDefaultXdsServerUrl, false, true)}));
  // Start a watch for "foo1".
  auto watcher = StartFooWatch("foo1");
  // Watcher should initially not see any resource reported.
  EXPECT_FALSE(watcher->HasEvent());
  // Check metric data.
  EXPECT_THAT(metrics_reporter_->resource_updates_valid(),
              ::testing::ElementsAre());
  EXPECT_THAT(metrics_reporter_->resource_updates_invalid(),
              ::testing::ElementsAre());
  EXPECT_THAT(GetResourceCounts(),
              ::testing::ElementsAre(::testing::Pair(
                  ResourceCountLabelsEq(XdsClient::kOldStyleAuthority,
                                        XdsFooResourceType::Get()->type_url(),
                                        "requested"),
                  1)));
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
  EXPECT_TRUE(watcher->WaitForDoesNotExist());
  // Check metric data.
  EXPECT_TRUE(metrics_reporter_->WaitForMetricsReporterData(
      ::testing::ElementsAre(), ::testing::ElementsAre(), ::testing::_));
  EXPECT_THAT(GetResourceCounts(),
              ::testing::ElementsAre(::testing::Pair(
                  ResourceCountLabelsEq(XdsClient::kOldStyleAuthority,
                                        XdsFooResourceType::Get()->type_url(),
                                        "does_not_exist"),
                  1)));
  // Check CSDS data.
  ClientConfig csds = DumpCsds();
  EXPECT_THAT(csds.generic_xds_configs(),
              ::testing::ElementsAre(CsdsResourceDoesNotExistOnTimeout(
                  XdsFooResourceType::Get()->type_url(), "foo1")));
  // Start a new watcher for the same resource.  It should immediately
  // receive the same does-not-exist notification.
  auto watcher2 = StartFooWatch("foo1");
  EXPECT_TRUE(watcher2->WaitForDoesNotExist());
  // Now server sends a response.
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("1")
          .set_nonce("A")
          .AddFooResource(XdsFooResource("foo1", 6))
          .Serialize());
  // XdsClient should have delivered the response to the watchers.
  auto resource = watcher->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  resource = watcher2->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  // Check metric data.
  EXPECT_TRUE(metrics_reporter_->WaitForMetricsReporterData(
      ::testing::ElementsAre(::testing::Pair(
          ::testing::Pair(kDefaultXdsServerUrl,
                          XdsFooResourceType::Get()->type_url()),
          1)),
      ::testing::ElementsAre(), ::testing::_));
  EXPECT_THAT(
      GetResourceCounts(),
      ::testing::ElementsAre(::testing::Pair(
          ResourceCountLabelsEq(XdsClient::kOldStyleAuthority,
                                XdsFooResourceType::Get()->type_url(), "acked"),
          1)));
  // Check CSDS data.
  csds = DumpCsds();
  EXPECT_THAT(csds.generic_xds_configs(),
              ::testing::UnorderedElementsAre(CsdsResourceAcked(
                  XdsFooResourceType::Get()->type_url(), "foo1",
                  resource->AsJsonString(), "1", TimestampProtoEq(kTime0))));
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
  EXPECT_TRUE(stream->IsOrphaned());
}

TEST_F(XdsClientTest, ResourceTimerIsTransientFailure) {
  testing::ScopedExperimentalEnvVar env_var(
      "GRPC_EXPERIMENTAL_XDS_DATA_ERROR_HANDLING");
  event_engine_->SetRunAfterDurationCallback(
      [&](grpc_event_engine::experimental::EventEngine::Duration duration) {
        grpc_event_engine::experimental::EventEngine::Duration expected =
            std::chrono::seconds(30);
        EXPECT_EQ(duration, expected) << "Expected: " << expected.count()
                                      << "\nActual:   " << duration.count();
      });
  InitXdsClient(FakeXdsBootstrap::Builder().SetServers(
      {FakeXdsBootstrap::FakeXdsServer(kDefaultXdsServerUrl, false, true)}));
  // Start a watch for "foo1".
  auto watcher = StartFooWatch("foo1");
  // Watcher should initially not see any resource reported.
  EXPECT_FALSE(watcher->HasEvent());
  // Check metric data.
  EXPECT_THAT(metrics_reporter_->resource_updates_valid(),
              ::testing::ElementsAre());
  EXPECT_THAT(metrics_reporter_->resource_updates_invalid(),
              ::testing::ElementsAre());
  EXPECT_THAT(GetResourceCounts(),
              ::testing::ElementsAre(::testing::Pair(
                  ResourceCountLabelsEq(XdsClient::kOldStyleAuthority,
                                        XdsFooResourceType::Get()->type_url(),
                                        "requested"),
                  1)));
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
  auto error = watcher->WaitForNextError();
  ASSERT_TRUE(error.has_value());
  EXPECT_EQ(error, absl::UnavailableError(absl::StrCat(
                       "timeout obtaining resource from xDS server ",
                       kDefaultXdsServerUrl, " (node ID:xds_client_test)")));
  // Check metric data.
  EXPECT_TRUE(metrics_reporter_->WaitForMetricsReporterData(
      ::testing::ElementsAre(), ::testing::ElementsAre(), ::testing::_));
  EXPECT_THAT(GetResourceCounts(),
              ::testing::ElementsAre(::testing::Pair(
                  ResourceCountLabelsEq(XdsClient::kOldStyleAuthority,
                                        XdsFooResourceType::Get()->type_url(),
                                        "timeout"),
                  1)));
  // Check CSDS data.
  ClientConfig csds = DumpCsds();
  EXPECT_THAT(
      csds.generic_xds_configs(),
      ::testing::UnorderedElementsAre(CsdsResourceEq(
          ClientResourceStatus::TIMEOUT, XdsFooResourceType::Get()->type_url(),
          "foo1", CsdsNoResourceFields(),
          CsdsErrorDetailsOnly(
              absl::StrCat("timeout obtaining resource from xDS server ",
                           kDefaultXdsServerUrl)))));
  // Start a new watcher for the same resource.  It should immediately
  // receive the same does-not-exist notification.
  auto watcher2 = StartFooWatch("foo1");
  error = watcher2->WaitForNextError();
  ASSERT_TRUE(error.has_value());
  EXPECT_EQ(error, absl::UnavailableError(absl::StrCat(
                       "timeout obtaining resource from xDS server ",
                       kDefaultXdsServerUrl, " (node ID:xds_client_test)")));
  // Now server sends a response.
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("1")
          .set_nonce("A")
          .AddFooResource(XdsFooResource("foo1", 6))
          .Serialize());
  // XdsClient should have delivered the response to the watchers.
  auto resource = watcher->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  resource = watcher2->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  // Check metric data.
  EXPECT_TRUE(metrics_reporter_->WaitForMetricsReporterData(
      ::testing::ElementsAre(::testing::Pair(
          ::testing::Pair(kDefaultXdsServerUrl,
                          XdsFooResourceType::Get()->type_url()),
          1)),
      ::testing::ElementsAre(), ::testing::_));
  EXPECT_THAT(
      GetResourceCounts(),
      ::testing::ElementsAre(::testing::Pair(
          ResourceCountLabelsEq(XdsClient::kOldStyleAuthority,
                                XdsFooResourceType::Get()->type_url(), "acked"),
          1)));
  // Check CSDS data.
  csds = DumpCsds();
  EXPECT_THAT(csds.generic_xds_configs(),
              ::testing::UnorderedElementsAre(CsdsResourceAcked(
                  XdsFooResourceType::Get()->type_url(), "foo1",
                  resource->AsJsonString(), "1", TimestampProtoEq(kTime0))));
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
  EXPECT_TRUE(stream->IsOrphaned());
}

TEST_F(XdsClientTest, ResourceDoesNotExistAfterStreamRestart) {
  InitXdsClient();
  // Metrics should initially be empty.
  EXPECT_THAT(metrics_reporter_->resource_updates_valid(),
              ::testing::ElementsAre());
  EXPECT_THAT(metrics_reporter_->resource_updates_invalid(),
              ::testing::ElementsAre());
  EXPECT_THAT(GetResourceCounts(), ::testing::ElementsAre());
  // Start a watch for "foo1".
  auto watcher = StartFooWatch("foo1");
  // Check metric data.
  EXPECT_THAT(metrics_reporter_->resource_updates_valid(),
              ::testing::ElementsAre());
  EXPECT_THAT(metrics_reporter_->resource_updates_invalid(),
              ::testing::ElementsAre());
  EXPECT_THAT(GetResourceCounts(),
              ::testing::ElementsAre(::testing::Pair(
                  ResourceCountLabelsEq(XdsClient::kOldStyleAuthority,
                                        XdsFooResourceType::Get()->type_url(),
                                        "requested"),
                  1)));
  // CSDS should show that the resource has been requested.
  ClientConfig csds = DumpCsds();
  EXPECT_THAT(csds.generic_xds_configs(),
              ::testing::ElementsAre(CsdsResourceRequested(
                  XdsFooResourceType::Get()->type_url(), "foo1")));
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
  // Check metric data.
  EXPECT_TRUE(metrics_reporter_->WaitForMetricsReporterData(
      ::testing::ElementsAre(), ::testing::ElementsAre(), ::testing::_));
  EXPECT_THAT(GetResourceCounts(),
              ::testing::ElementsAre(::testing::Pair(
                  ResourceCountLabelsEq(XdsClient::kOldStyleAuthority,
                                        XdsFooResourceType::Get()->type_url(),
                                        "requested"),
                  1)));
  // CSDS should show that the resource has been requested.
  csds = DumpCsds();
  EXPECT_THAT(csds.generic_xds_configs(),
              ::testing::ElementsAre(CsdsResourceRequested(
                  XdsFooResourceType::Get()->type_url(), "foo1")));
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
  ASSERT_TRUE(watcher->WaitForDoesNotExist());
  // Check metric data.
  EXPECT_TRUE(metrics_reporter_->WaitForMetricsReporterData(
      ::testing::ElementsAre(), ::testing::ElementsAre(), ::testing::_));
  EXPECT_THAT(GetResourceCounts(),
              ::testing::ElementsAre(::testing::Pair(
                  ResourceCountLabelsEq(XdsClient::kOldStyleAuthority,
                                        XdsFooResourceType::Get()->type_url(),
                                        "does_not_exist"),
                  1)));
  // CSDS should show that the resource has been requested.
  csds = DumpCsds();
  EXPECT_THAT(csds.generic_xds_configs(),
              ::testing::ElementsAre(CsdsResourceDoesNotExistOnTimeout(
                  XdsFooResourceType::Get()->type_url(), "foo1")));
  // Server now sends the requested resource.
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("1")
          .set_nonce("A")
          .AddFooResource(XdsFooResource("foo1", 6))
          .Serialize());
  // The resource is delivered to the watcher.
  auto resource = watcher->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  // Check metric data.
  EXPECT_TRUE(metrics_reporter_->WaitForMetricsReporterData(
      ::testing::ElementsAre(::testing::Pair(
          ::testing::Pair(kDefaultXdsServerUrl,
                          XdsFooResourceType::Get()->type_url()),
          1)),
      ::testing::ElementsAre(), ::testing::_));
  EXPECT_THAT(
      GetResourceCounts(),
      ::testing::ElementsAre(::testing::Pair(
          ResourceCountLabelsEq(XdsClient::kOldStyleAuthority,
                                XdsFooResourceType::Get()->type_url(), "acked"),
          1)));
  // Check CSDS data.
  csds = DumpCsds();
  EXPECT_THAT(csds.generic_xds_configs(),
              ::testing::UnorderedElementsAre(CsdsResourceAcked(
                  XdsFooResourceType::Get()->type_url(), "foo1",
                  resource->AsJsonString(), "1", TimestampProtoEq(kTime0))));
  // XdsClient sends an ACK.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  // Cancel watcher.
  CancelFooWatch(watcher.get(), "foo1");
  EXPECT_TRUE(stream->IsOrphaned());
}

TEST_F(XdsClientTest, DoesNotExistTimerNotStartedUntilSendCompletes) {
  InitXdsClient();
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
  EXPECT_TRUE(watcher->ExpectNoEvent());
  // Check metric data.
  EXPECT_THAT(GetResourceCounts(),
              ::testing::ElementsAre(::testing::Pair(
                  ResourceCountLabelsEq(XdsClient::kOldStyleAuthority,
                                        XdsFooResourceType::Get()->type_url(),
                                        "requested"),
                  1)));
  // CSDS should show that the resource has been requested.
  ClientConfig csds = DumpCsds();
  EXPECT_THAT(csds.generic_xds_configs(),
              ::testing::ElementsAre(CsdsResourceRequested(
                  XdsFooResourceType::Get()->type_url(), "foo1")));
  // The ADS stream uses wait_for_ready inside the XdsTransport interface,
  // so when the channel connects, the already-started stream will proceed.
  stream->CompleteSendMessageFromClient();
  // Server does NOT send a response.
  // Watcher should see a does-not-exist event.
  EXPECT_TRUE(watcher->WaitForDoesNotExist());
  // Check metric data.
  EXPECT_THAT(GetResourceCounts(),
              ::testing::ElementsAre(::testing::Pair(
                  ResourceCountLabelsEq(XdsClient::kOldStyleAuthority,
                                        XdsFooResourceType::Get()->type_url(),
                                        "does_not_exist"),
                  1)));
  // CSDS should show that the resource has been requested.
  csds = DumpCsds();
  EXPECT_THAT(csds.generic_xds_configs(),
              ::testing::ElementsAre(CsdsResourceDoesNotExistOnTimeout(
                  XdsFooResourceType::Get()->type_url(), "foo1")));
  // Now server sends a response.
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("1")
          .set_nonce("A")
          .AddFooResource(XdsFooResource("foo1", 6))
          .Serialize());
  // XdsClient should have delivered the response to the watcher.
  auto resource = watcher->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  // Check metric data.
  EXPECT_THAT(
      GetResourceCounts(),
      ::testing::ElementsAre(::testing::Pair(
          ResourceCountLabelsEq(XdsClient::kOldStyleAuthority,
                                XdsFooResourceType::Get()->type_url(), "acked"),
          1)));
  // Check CSDS data.
  csds = DumpCsds();
  EXPECT_THAT(csds.generic_xds_configs(),
              ::testing::UnorderedElementsAre(CsdsResourceAcked(
                  XdsFooResourceType::Get()->type_url(), "foo1",
                  resource->AsJsonString(), "1", TimestampProtoEq(kTime0))));
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
  EXPECT_TRUE(stream->IsOrphaned());
}

// In https://github.com/grpc/grpc/issues/29583, we ran into a case
// where we wound up starting a timer after we had already received the
// resource, thus incorrectly reporting the resource as not existing.
// This happened when unsubscribing and then resubscribing to the same
// resource while a send_message op was already in flight and then
// receiving an update containing that resource.
TEST_F(XdsClientTest,
       ResourceDoesNotExistUnsubscribeAndResubscribeWhileSendMessagePending) {
  InitXdsClient();
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
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  // Check metric data.
  EXPECT_TRUE(metrics_reporter_->WaitForMetricsReporterData(
      ::testing::ElementsAre(::testing::Pair(
          ::testing::Pair(kDefaultXdsServerUrl,
                          XdsFooResourceType::Get()->type_url()),
          1)),
      ::testing::ElementsAre(), ::testing::_));
  EXPECT_THAT(
      GetResourceCounts(),
      ::testing::ElementsAre(::testing::Pair(
          ResourceCountLabelsEq(XdsClient::kOldStyleAuthority,
                                XdsFooResourceType::Get()->type_url(), "acked"),
          1)));
  // Check CSDS data.
  ClientConfig csds = DumpCsds();
  EXPECT_THAT(csds.generic_xds_configs(),
              ::testing::UnorderedElementsAre(CsdsResourceAcked(
                  XdsFooResourceType::Get()->type_url(), "foo1",
                  resource->AsJsonString(), "1", TimestampProtoEq(kTime0))));
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
  // Check metric data.
  EXPECT_THAT(
      GetResourceCounts(),
      ::testing::ElementsAre(
          ::testing::Pair(ResourceCountLabelsEq(
                              XdsClient::kOldStyleAuthority,
                              XdsFooResourceType::Get()->type_url(), "acked"),
                          1),
          ::testing::Pair(
              ResourceCountLabelsEq(XdsClient::kOldStyleAuthority,
                                    XdsFooResourceType::Get()->type_url(),
                                    "requested"),
              1)));
  // Check CSDS data.
  csds = DumpCsds();
  EXPECT_THAT(csds.generic_xds_configs(),
              ::testing::UnorderedElementsAre(
                  CsdsResourceAcked(XdsFooResourceType::Get()->type_url(),
                                    "foo1", resource->AsJsonString(), "1",
                                    TimestampProtoEq(kTime0)),
                  CsdsResourceRequested(XdsFooResourceType::Get()->type_url(),
                                        "foo2")));
  // XdsClient sends a request to subscribe to the new resource.
  // NOTE: We do NOT yet tell the XdsClient that the send_message op is
  // complete.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1", "foo2"});
  // Unsubscribe from foo1.
  CancelFooWatch(watcher.get(), "foo1");
  EXPECT_THAT(GetResourceCounts(),
              ::testing::ElementsAre(::testing::Pair(
                  ResourceCountLabelsEq(XdsClient::kOldStyleAuthority,
                                        XdsFooResourceType::Get()->type_url(),
                                        "requested"),
                  1)));
  csds = DumpCsds();
  EXPECT_THAT(csds.generic_xds_configs(),
              ::testing::ElementsAre(CsdsResourceRequested(
                  XdsFooResourceType::Get()->type_url(), "foo2")));
  // Now immediately resubscribe to foo1.
  // The watcher will receive an update immediately from the cache.
  watcher = StartFooWatch("foo1");
  resource = watcher->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  EXPECT_THAT(
      GetResourceCounts(),
      ::testing::ElementsAre(
          ::testing::Pair(ResourceCountLabelsEq(
                              XdsClient::kOldStyleAuthority,
                              XdsFooResourceType::Get()->type_url(), "acked"),
                          1),
          ::testing::Pair(
              ResourceCountLabelsEq(XdsClient::kOldStyleAuthority,
                                    XdsFooResourceType::Get()->type_url(),
                                    "requested"),
              1)));
  csds = DumpCsds();
  EXPECT_THAT(csds.generic_xds_configs(),
              ::testing::ElementsAre(
                  CsdsResourceAcked(XdsFooResourceType::Get()->type_url(),
                                    "foo1", resource->AsJsonString(), "1",
                                    TimestampProtoEq(kTime0)),
                  CsdsResourceRequested(XdsFooResourceType::Get()->type_url(),
                                        "foo2")));
  // Now send a response from the server containing both foo1 and foo2.
  // We increment time to make sure that the CSDS data gets a new timestamp.
  time_cache_.TestOnlySetNow(kTime1);
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("1")
          .set_nonce("B")
          .AddFooResource(XdsFooResource("foo1", 6))
          .AddFooResource(XdsFooResource("foo2", 7))
          .Serialize());
  // The watcher for foo1 won't receive an update, since the resource
  // hasn't changed.
  EXPECT_TRUE(watcher->ExpectNoEvent());
  // For foo2, the watcher should receive notification for the new resource.
  auto resource2 = watcher2->WaitForNextResource();
  ASSERT_NE(resource2, nullptr);
  EXPECT_EQ(resource2->name, "foo2");
  EXPECT_EQ(resource2->value, 7);
  // Check metric data.
  EXPECT_TRUE(metrics_reporter_->WaitForMetricsReporterData(
      ::testing::ElementsAre(::testing::Pair(
          ::testing::Pair(kDefaultXdsServerUrl,
                          XdsFooResourceType::Get()->type_url()),
          3)),
      ::testing::ElementsAre(), ::testing::_));
  EXPECT_THAT(
      GetResourceCounts(),
      ::testing::ElementsAre(::testing::Pair(
          ResourceCountLabelsEq(XdsClient::kOldStyleAuthority,
                                XdsFooResourceType::Get()->type_url(), "acked"),
          2)));
  // Check CSDS data.
  csds = DumpCsds();
  EXPECT_THAT(csds.generic_xds_configs(),
              ::testing::UnorderedElementsAre(
                  CsdsResourceAcked(XdsFooResourceType::Get()->type_url(),
                                    "foo1", resource->AsJsonString(), "1",
                                    TimestampProtoEq(kTime1)),
                  CsdsResourceAcked(XdsFooResourceType::Get()->type_url(),
                                    "foo2", resource2->AsJsonString(), "1",
                                    TimestampProtoEq(kTime1))));
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
  EXPECT_TRUE(watcher->ExpectNoEvent());
  // Cancel watches.
  CancelFooWatch(watcher.get(), "foo1", /*delay_unsubscription=*/true);
  CancelFooWatch(watcher2.get(), "foo2");
  EXPECT_TRUE(stream->IsOrphaned());
}

TEST_F(XdsClientTest, DoNotSendDoesNotExistForCachedResource) {
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
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  // Check metric data.
  EXPECT_TRUE(metrics_reporter_->WaitForMetricsReporterData(
      ::testing::ElementsAre(::testing::Pair(
          ::testing::Pair(kDefaultXdsServerUrl,
                          XdsFooResourceType::Get()->type_url()),
          1)),
      ::testing::ElementsAre(), ::testing::_));
  EXPECT_THAT(
      GetResourceCounts(),
      ::testing::ElementsAre(::testing::Pair(
          ResourceCountLabelsEq(XdsClient::kOldStyleAuthority,
                                XdsFooResourceType::Get()->type_url(), "acked"),
          1)));
  // Check CSDS data.
  ClientConfig csds = DumpCsds();
  EXPECT_THAT(csds.generic_xds_configs(),
              ::testing::UnorderedElementsAre(CsdsResourceAcked(
                  XdsFooResourceType::Get()->type_url(), "foo1",
                  resource->AsJsonString(), "1", TimestampProtoEq(kTime0))));
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
  EXPECT_TRUE(watcher->ExpectNoEvent());
  // Check metric data.
  EXPECT_TRUE(metrics_reporter_->WaitForMetricsReporterData(
      ::testing::ElementsAre(::testing::Pair(
          ::testing::Pair(kDefaultXdsServerUrl,
                          XdsFooResourceType::Get()->type_url()),
          1)),
      ::testing::ElementsAre(), ::testing::_));
  EXPECT_THAT(
      GetResourceCounts(),
      ::testing::ElementsAre(::testing::Pair(
          ResourceCountLabelsEq(XdsClient::kOldStyleAuthority,
                                XdsFooResourceType::Get()->type_url(), "acked"),
          1)));
  // Check CSDS data.
  csds = DumpCsds();
  EXPECT_THAT(csds.generic_xds_configs(),
              ::testing::UnorderedElementsAre(CsdsResourceAcked(
                  XdsFooResourceType::Get()->type_url(), "foo1",
                  resource->AsJsonString(), "1", TimestampProtoEq(kTime0))));
  // Now server sends a response.
  // We increment time to make sure that the CSDS data gets a new timestamp.
  time_cache_.TestOnlySetNow(kTime1);
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("1")
          .set_nonce("A")
          .AddFooResource(XdsFooResource("foo1", 6))
          .Serialize());
  // Watcher will not see any update, since the resource is unchanged.
  EXPECT_TRUE(watcher->ExpectNoEvent());
  // Check metric data.
  EXPECT_TRUE(metrics_reporter_->WaitForMetricsReporterData(
      ::testing::ElementsAre(::testing::Pair(
          ::testing::Pair(kDefaultXdsServerUrl,
                          XdsFooResourceType::Get()->type_url()),
          2)),
      ::testing::ElementsAre(), ::testing::_));
  EXPECT_THAT(
      GetResourceCounts(),
      ::testing::ElementsAre(::testing::Pair(
          ResourceCountLabelsEq(XdsClient::kOldStyleAuthority,
                                XdsFooResourceType::Get()->type_url(), "acked"),
          1)));
  EXPECT_THAT(GetServerConnections(), ::testing::ElementsAre(::testing::Pair(
                                          kDefaultXdsServerUrl, true)));
  // Check CSDS data.
  csds = DumpCsds();
  EXPECT_THAT(csds.generic_xds_configs(),
              ::testing::UnorderedElementsAre(CsdsResourceAcked(
                  XdsFooResourceType::Get()->type_url(), "foo1",
                  resource->AsJsonString(), "1", TimestampProtoEq(kTime1))));
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  // Cancel watch.
  CancelFooWatch(watcher.get(), "foo1");
  EXPECT_TRUE(stream->IsOrphaned());
}

TEST_F(XdsClientTest, UnsubscribeAndResubscribeRace) {
  InitXdsClient();
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
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  // Check metric data.
  EXPECT_TRUE(metrics_reporter_->WaitForMetricsReporterData(
      ::testing::ElementsAre(::testing::Pair(
          ::testing::Pair(kDefaultXdsServerUrl,
                          XdsFooResourceType::Get()->type_url()),
          1)),
      ::testing::ElementsAre(), ::testing::_));
  EXPECT_THAT(
      GetResourceCounts(),
      ::testing::ElementsAre(::testing::Pair(
          ResourceCountLabelsEq(XdsClient::kOldStyleAuthority,
                                XdsFooResourceType::Get()->type_url(), "acked"),
          1)));
  // Check CSDS data.
  ClientConfig csds = DumpCsds();
  EXPECT_THAT(csds.generic_xds_configs(),
              ::testing::UnorderedElementsAre(CsdsResourceAcked(
                  XdsFooResourceType::Get()->type_url(), "foo1",
                  resource->AsJsonString(), "1", TimestampProtoEq(kTime0))));
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
  // Check metric data.
  EXPECT_THAT(
      GetResourceCounts(),
      ::testing::ElementsAre(
          ::testing::Pair(ResourceCountLabelsEq(
                              XdsClient::kOldStyleAuthority,
                              XdsFooResourceType::Get()->type_url(), "acked"),
                          1),
          ::testing::Pair(
              ResourceCountLabelsEq(XdsClient::kOldStyleAuthority,
                                    XdsFooResourceType::Get()->type_url(),
                                    "requested"),
              1)));
  // Check CSDS data.
  csds = DumpCsds();
  EXPECT_THAT(csds.generic_xds_configs(),
              ::testing::UnorderedElementsAre(
                  CsdsResourceAcked(XdsFooResourceType::Get()->type_url(),
                                    "foo1", resource->AsJsonString(), "1",
                                    TimestampProtoEq(kTime0)),
                  CsdsResourceRequested(XdsFooResourceType::Get()->type_url(),
                                        "foo2")));
  // XdsClient sends a request to subscribe to the new resource.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1", "foo2"});
  stream->CompleteSendMessageFromClient();
  // Send a response from the server containing both foo1 and foo2.
  // We increment time to make sure that the CSDS data gets a new timestamp.
  time_cache_.TestOnlySetNow(kTime1);
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("1")
          .set_nonce("B")
          .AddFooResource(XdsFooResource("foo1", 6))
          .AddFooResource(XdsFooResource("foo2", 7))
          .Serialize());
  // The watcher for foo2 should receive notification for the new resource.
  auto resource2 = watcher2->WaitForNextResource();
  ASSERT_NE(resource2, nullptr);
  EXPECT_EQ(resource2->name, "foo2");
  EXPECT_EQ(resource2->value, 7);
  // Check metric data.
  EXPECT_TRUE(metrics_reporter_->WaitForMetricsReporterData(
      ::testing::ElementsAre(::testing::Pair(
          ::testing::Pair(kDefaultXdsServerUrl,
                          XdsFooResourceType::Get()->type_url()),
          3)),
      ::testing::ElementsAre(), ::testing::_));
  EXPECT_THAT(
      GetResourceCounts(),
      ::testing::ElementsAre(::testing::Pair(
          ResourceCountLabelsEq(XdsClient::kOldStyleAuthority,
                                XdsFooResourceType::Get()->type_url(), "acked"),
          2)));
  // Check CSDS data.
  csds = DumpCsds();
  EXPECT_THAT(csds.generic_xds_configs(),
              ::testing::UnorderedElementsAre(
                  CsdsResourceAcked(XdsFooResourceType::Get()->type_url(),
                                    "foo1", resource->AsJsonString(), "1",
                                    TimestampProtoEq(kTime1)),
                  CsdsResourceAcked(XdsFooResourceType::Get()->type_url(),
                                    "foo2", resource2->AsJsonString(), "1",
                                    TimestampProtoEq(kTime1))));
  // XdsClient should have sent an ACK message to the xDS server.
  // NOTE: We do NOT yet tell the XdsClient that the send_message op is
  // complete.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"B",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1", "foo2"});
  // Unsubscribe from foo1.  Because the previous send_message op is
  // still in flight, we cannot immediately send the unsubscription
  // message, so the resource won't actually be removed from the cache
  // yet, although it will not show up in metrics or in CSDS.
  CancelFooWatch(watcher.get(), "foo1");
  EXPECT_THAT(
      GetResourceCounts(),
      ::testing::ElementsAre(::testing::Pair(
          ResourceCountLabelsEq(XdsClient::kOldStyleAuthority,
                                XdsFooResourceType::Get()->type_url(), "acked"),
          1)));
  csds = DumpCsds();
  EXPECT_THAT(csds.generic_xds_configs(),
              ::testing::ElementsAre(CsdsResourceAcked(
                  XdsFooResourceType::Get()->type_url(), "foo2",
                  resource2->AsJsonString(), "1", TimestampProtoEq(kTime1))));
  // Immediately resubscribe to foo1.  Cache entry should already be
  // present, because we should not yet have deleted it.
  watcher = StartFooWatch("foo1");
  EXPECT_THAT(
      GetResourceCounts(),
      ::testing::ElementsAre(::testing::Pair(
          ResourceCountLabelsEq(XdsClient::kOldStyleAuthority,
                                XdsFooResourceType::Get()->type_url(), "acked"),
          2)));
  csds = DumpCsds();
  EXPECT_THAT(csds.generic_xds_configs(),
              ::testing::UnorderedElementsAre(
                  CsdsResourceAcked(XdsFooResourceType::Get()->type_url(),
                                    "foo1", resource->AsJsonString(), "1",
                                    TimestampProtoEq(kTime1)),
                  CsdsResourceAcked(XdsFooResourceType::Get()->type_url(),
                                    "foo2", resource2->AsJsonString(), "1",
                                    TimestampProtoEq(kTime1))));
  // The watcher for foo1 will receive an immediate update, since the
  // resource is still present in the cache.
  resource = watcher->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  // Now we finally tell XdsClient that its previous send_message op is
  // complete.
  stream->CompleteSendMessageFromClient();
  // XdsClient will send a new subscription request here.  It doesn't
  // actually need to do this, since the list of subscribed resources
  // hasn't actually changed, but the implementation doesn't know that.
  // We could in theory avoid this, but it would be more work.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"B",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1", "foo2"});
  stream->CompleteSendMessageFromClient();
  // Make sure the watcher for foo1 does not see a does-not-exist event.
  EXPECT_TRUE(watcher->ExpectNoEvent());
  // Cancel watches.
  CancelFooWatch(watcher.get(), "foo1", /*delay_unsubscription=*/true);
  CancelFooWatch(watcher2.get(), "foo2");
  EXPECT_TRUE(stream->IsOrphaned());
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
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  // Check metric data.
  EXPECT_TRUE(metrics_reporter_->WaitForMetricsReporterData(
      ::testing::ElementsAre(::testing::Pair(
          ::testing::Pair(kDefaultXdsServerUrl,
                          XdsFooResourceType::Get()->type_url()),
          1)),
      ::testing::ElementsAre(), ::testing::_));
  EXPECT_THAT(
      GetResourceCounts(),
      ::testing::ElementsAre(::testing::Pair(
          ResourceCountLabelsEq(XdsClient::kOldStyleAuthority,
                                XdsFooResourceType::Get()->type_url(), "acked"),
          1)));
  // Check CSDS data.
  ClientConfig csds = DumpCsds();
  EXPECT_THAT(csds.generic_xds_configs(),
              ::testing::UnorderedElementsAre(CsdsResourceAcked(
                  XdsFooResourceType::Get()->type_url(), "foo1",
                  resource->AsJsonString(), "1", TimestampProtoEq(kTime0))));
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  // Cancel watch.
  CancelFooWatch(watcher.get(), "foo1");
  EXPECT_TRUE(stream->IsOrphaned());
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
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  // Check metric data.
  EXPECT_TRUE(metrics_reporter_->WaitForMetricsReporterData(
      ::testing::ElementsAre(::testing::Pair(
          ::testing::Pair(kDefaultXdsServerUrl,
                          XdsFooResourceType::Get()->type_url()),
          1)),
      ::testing::ElementsAre(), ::testing::_));
  EXPECT_THAT(
      GetResourceCounts(),
      ::testing::ElementsAre(::testing::Pair(
          ResourceCountLabelsEq(XdsClient::kOldStyleAuthority,
                                XdsFooResourceType::Get()->type_url(), "acked"),
          1)));
  // Check CSDS data.
  ClientConfig csds = DumpCsds();
  EXPECT_THAT(csds.generic_xds_configs(),
              ::testing::UnorderedElementsAre(CsdsResourceAcked(
                  XdsFooResourceType::Get()->type_url(), "foo1",
                  resource->AsJsonString(), "1", TimestampProtoEq(kTime0))));
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
  // We increment time to make sure that the CSDS data gets a new timestamp.
  time_cache_.TestOnlySetNow(kTime1);
  stream->SendMessageToClient(
      ResponseBuilder(XdsBarResourceType::Get()->type_url())
          .set_version_info("2")
          .set_nonce("B")
          .AddBarResource(XdsBarResource("bar1", "whee"))
          .Serialize());
  // XdsClient should have delivered the response to the watcher.
  auto resource2 = watcher2->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource2->name, "bar1");
  EXPECT_EQ(resource2->value, "whee");
  // Check metric data.
  EXPECT_TRUE(metrics_reporter_->WaitForMetricsReporterData(
      ::testing::ElementsAre(
          ::testing::Pair(
              ::testing::Pair(kDefaultXdsServerUrl,
                              XdsBarResourceType::Get()->type_url()),
              1),
          ::testing::Pair(
              ::testing::Pair(kDefaultXdsServerUrl,
                              XdsFooResourceType::Get()->type_url()),
              1)),
      ::testing::ElementsAre(), ::testing::_));
  EXPECT_THAT(
      GetResourceCounts(),
      ::testing::UnorderedElementsAre(
          ::testing::Pair(ResourceCountLabelsEq(
                              XdsClient::kOldStyleAuthority,
                              XdsBarResourceType::Get()->type_url(), "acked"),
                          1),
          ::testing::Pair(ResourceCountLabelsEq(
                              XdsClient::kOldStyleAuthority,
                              XdsFooResourceType::Get()->type_url(), "acked"),
                          1)));
  // Check CSDS data.
  csds = DumpCsds();
  EXPECT_THAT(csds.generic_xds_configs(),
              ::testing::UnorderedElementsAre(
                  CsdsResourceAcked(XdsFooResourceType::Get()->type_url(),
                                    "foo1", resource->AsJsonString(), "1",
                                    TimestampProtoEq(kTime0)),
                  CsdsResourceAcked(XdsBarResourceType::Get()->type_url(),
                                    "bar1", resource2->AsJsonString(), "2",
                                    TimestampProtoEq(kTime1))));
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
  // Server sends an empty response for the resource type.
  // (The server doesn't need to do this, but it may.)
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("1")
          .set_nonce("C")
          .Serialize());
  // Client should ACK.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"C",
               /*error_detail=*/absl::OkStatus(), /*resource_names=*/{});
  // Now subscribe to foo2.
  watcher = StartFooWatch("foo2");
  // Client sends a subscription request, which retains the nonce and
  // version seen previously.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"C",
               /*error_detail=*/absl::OkStatus(), /*resource_names=*/{"foo2"});
  // Server sends foo2.
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("1")
          .set_nonce("D")
          .AddFooResource(XdsFooResource("foo2", 8))
          .Serialize());
  // Watcher receives the resource.
  resource = watcher->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo2");
  EXPECT_EQ(resource->value, 8);
  // Client ACKs.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"D",
               /*error_detail=*/absl::OkStatus(), /*resource_names=*/{"foo2"});
  // Cancel watches.
  CancelFooWatch(watcher.get(), "foo2", /*delay_unsubscription=*/true);
  CancelBarWatch(watcher2.get(), "bar1");
  EXPECT_TRUE(stream->IsOrphaned());
}

TEST_F(XdsClientTest, Federation) {
  constexpr char kAuthority[] = "xds.example.com";
  const std::string kXdstpResourceName = absl::StrCat(
      "xdstp://", kAuthority, "/", XdsFooResource::TypeUrl(), "/foo2");
  FakeXdsBootstrap::FakeXdsServer authority_server("other_xds_server");
  FakeXdsBootstrap::FakeAuthority authority;
  authority.set_server(authority_server);
  InitXdsClient(
      FakeXdsBootstrap::Builder().AddAuthority(kAuthority, authority));
  // Metrics should initially be empty.
  EXPECT_THAT(metrics_reporter_->resource_updates_valid(),
              ::testing::ElementsAre());
  EXPECT_THAT(metrics_reporter_->resource_updates_invalid(),
              ::testing::ElementsAre());
  EXPECT_THAT(GetResourceCounts(), ::testing::ElementsAre());
  EXPECT_THAT(GetServerConnections(), ::testing::ElementsAre());
  // Start a watch for "foo1".
  auto watcher = StartFooWatch("foo1");
  // Watcher should initially not see any resource reported.
  EXPECT_FALSE(watcher->HasEvent());
  // XdsClient should have created an ADS stream to the top-level xDS server.
  auto stream = WaitForAdsStream(*xds_client_->bootstrap().servers().front());
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
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  // Check metric data.
  EXPECT_TRUE(metrics_reporter_->WaitForMetricsReporterData(
      ::testing::ElementsAre(::testing::Pair(
          ::testing::Pair(kDefaultXdsServerUrl,
                          XdsFooResourceType::Get()->type_url()),
          1)),
      ::testing::ElementsAre(), ::testing::_));
  EXPECT_THAT(
      GetResourceCounts(),
      ::testing::ElementsAre(::testing::Pair(
          ResourceCountLabelsEq(XdsClient::kOldStyleAuthority,
                                XdsFooResourceType::Get()->type_url(), "acked"),
          1)));
  EXPECT_THAT(GetServerConnections(), ::testing::ElementsAre(::testing::Pair(
                                          kDefaultXdsServerUrl, true)));
  // Check CSDS data.
  ClientConfig csds = DumpCsds();
  EXPECT_THAT(csds.generic_xds_configs(),
              ::testing::UnorderedElementsAre(CsdsResourceAcked(
                  XdsFooResourceType::Get()->type_url(), "foo1",
                  resource->AsJsonString(), "1", TimestampProtoEq(kTime0))));
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
  // Check metric data.
  EXPECT_THAT(
      GetResourceCounts(),
      ::testing::ElementsAre(
          ::testing::Pair(ResourceCountLabelsEq(
                              XdsClient::kOldStyleAuthority,
                              XdsFooResourceType::Get()->type_url(), "acked"),
                          1),
          ::testing::Pair(ResourceCountLabelsEq(
                              kAuthority, XdsFooResourceType::Get()->type_url(),
                              "requested"),
                          1)));
  EXPECT_THAT(
      GetServerConnections(),
      ::testing::ElementsAre(
          ::testing::Pair(kDefaultXdsServerUrl, true),
          ::testing::Pair(authority_server.target()->server_uri(), true)));
  // Check CSDS data.
  csds = DumpCsds();
  EXPECT_THAT(csds.generic_xds_configs(),
              ::testing::UnorderedElementsAre(
                  CsdsResourceAcked(XdsFooResourceType::Get()->type_url(),
                                    "foo1", resource->AsJsonString(), "1",
                                    TimestampProtoEq(kTime0)),
                  CsdsResourceRequested(XdsFooResourceType::Get()->type_url(),
                                        kXdstpResourceName)));
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
  // We increment time to make sure that the CSDS data gets a new timestamp.
  time_cache_.TestOnlySetNow(kTime1);
  stream2->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("2")
          .set_nonce("B")
          .AddFooResource(XdsFooResource(kXdstpResourceName, 3))
          .Serialize());
  // XdsClient should have delivered the response to the watcher.
  auto resource2 = watcher2->WaitForNextResource();
  ASSERT_NE(resource2, nullptr);
  EXPECT_EQ(resource2->name, kXdstpResourceName);
  EXPECT_EQ(resource2->value, 3);
  // Check metric data.
  EXPECT_TRUE(metrics_reporter_->WaitForMetricsReporterData(
      ::testing::ElementsAre(
          ::testing::Pair(
              ::testing::Pair(kDefaultXdsServerUrl,
                              XdsFooResourceType::Get()->type_url()),
              1),
          ::testing::Pair(
              ::testing::Pair(authority_server.target()->server_uri(),
                              XdsFooResourceType::Get()->type_url()),
              1)),
      ::testing::ElementsAre(), ::testing::_));
  EXPECT_THAT(
      GetResourceCounts(),
      ::testing::ElementsAre(
          ::testing::Pair(ResourceCountLabelsEq(
                              XdsClient::kOldStyleAuthority,
                              XdsFooResourceType::Get()->type_url(), "acked"),
                          1),
          ::testing::Pair(
              ResourceCountLabelsEq(
                  kAuthority, XdsFooResourceType::Get()->type_url(), "acked"),
              1)));
  EXPECT_THAT(
      GetServerConnections(),
      ::testing::ElementsAre(
          ::testing::Pair(kDefaultXdsServerUrl, true),
          ::testing::Pair(authority_server.target()->server_uri(), true)));
  // Check CSDS data.
  csds = DumpCsds();
  EXPECT_THAT(
      csds.generic_xds_configs(),
      ::testing::UnorderedElementsAre(
          CsdsResourceAcked(XdsFooResourceType::Get()->type_url(), "foo1",
                            resource->AsJsonString(), "1",
                            TimestampProtoEq(kTime0)),
          CsdsResourceAcked(XdsFooResourceType::Get()->type_url(),
                            kXdstpResourceName, resource2->AsJsonString(), "2",
                            TimestampProtoEq(kTime1))));
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream2.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"2", /*response_nonce=*/"B",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{kXdstpResourceName});
  // Cancel watch for "foo1".
  CancelFooWatch(watcher.get(), "foo1");
  EXPECT_TRUE(stream->IsOrphaned());
  // Now cancel watch for xdstp resource name.
  CancelFooWatch(watcher2.get(), kXdstpResourceName);
  EXPECT_TRUE(stream2->IsOrphaned());
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
  auto stream = WaitForAdsStream(*xds_client_->bootstrap().servers().front());
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
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  // Check metric data.
  EXPECT_TRUE(metrics_reporter_->WaitForMetricsReporterData(
      ::testing::ElementsAre(::testing::Pair(
          ::testing::Pair(kDefaultXdsServerUrl,
                          XdsFooResourceType::Get()->type_url()),
          1)),
      ::testing::ElementsAre(), ::testing::_));
  EXPECT_THAT(
      GetResourceCounts(),
      ::testing::ElementsAre(::testing::Pair(
          ResourceCountLabelsEq(XdsClient::kOldStyleAuthority,
                                XdsFooResourceType::Get()->type_url(), "acked"),
          1)));
  EXPECT_THAT(GetServerConnections(), ::testing::ElementsAre(::testing::Pair(
                                          kDefaultXdsServerUrl, true)));
  // Check CSDS data.
  ClientConfig csds = DumpCsds();
  EXPECT_THAT(csds.generic_xds_configs(),
              ::testing::UnorderedElementsAre(CsdsResourceAcked(
                  XdsFooResourceType::Get()->type_url(), "foo1",
                  resource->AsJsonString(), "1", TimestampProtoEq(kTime0))));
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
  // We increment time to make sure that the CSDS data gets a new timestamp.
  time_cache_.TestOnlySetNow(kTime1);
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("2")
          .set_nonce("B")
          .AddFooResource(XdsFooResource(kXdstpResourceName, 3))
          .Serialize());
  // XdsClient should have delivered the response to the watcher.
  auto resource2 = watcher2->WaitForNextResource();
  ASSERT_NE(resource2, nullptr);
  EXPECT_EQ(resource2->name, kXdstpResourceName);
  EXPECT_EQ(resource2->value, 3);
  // Check metric data.
  EXPECT_TRUE(metrics_reporter_->WaitForMetricsReporterData(
      ::testing::ElementsAre(::testing::Pair(
          ::testing::Pair(kDefaultXdsServerUrl,
                          XdsFooResourceType::Get()->type_url()),
          2)),
      ::testing::ElementsAre(), ::testing::_));
  EXPECT_THAT(
      GetResourceCounts(),
      ::testing::ElementsAre(
          ::testing::Pair(ResourceCountLabelsEq(
                              XdsClient::kOldStyleAuthority,
                              XdsFooResourceType::Get()->type_url(), "acked"),
                          1),
          ::testing::Pair(
              ResourceCountLabelsEq(
                  kAuthority, XdsFooResourceType::Get()->type_url(), "acked"),
              1)));
  EXPECT_THAT(GetServerConnections(), ::testing::ElementsAre(::testing::Pair(
                                          kDefaultXdsServerUrl, true)));
  // Check CSDS data.
  csds = DumpCsds();
  EXPECT_THAT(
      csds.generic_xds_configs(),
      ::testing::UnorderedElementsAre(
          CsdsResourceAcked(XdsFooResourceType::Get()->type_url(), "foo1",
                            resource->AsJsonString(), "1",
                            TimestampProtoEq(kTime0)),
          CsdsResourceAcked(XdsFooResourceType::Get()->type_url(),
                            kXdstpResourceName, resource2->AsJsonString(), "2",
                            TimestampProtoEq(kTime1))));
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
  EXPECT_TRUE(stream->IsOrphaned());
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
  EXPECT_EQ(error->code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(error->message(),
            "authority \"xds.example.com\" not present in bootstrap config "
            "(node ID:xds_client_test)")
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
  EXPECT_EQ(error->code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(error->message(),
            "Unable to parse resource name xdstp://x "
            "(node ID:xds_client_test)")
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
  ASSERT_NE(resource, nullptr);
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
  EXPECT_TRUE(stream->IsOrphaned());
}

TEST_F(XdsClientTest, FederationChannelFailureReportedToWatchers) {
  constexpr char kAuthority[] = "xds.example.com";
  const std::string kXdstpResourceName = absl::StrCat(
      "xdstp://", kAuthority, "/", XdsFooResource::TypeUrl(), "/foo2");
  FakeXdsBootstrap::FakeXdsServer authority_server("other_xds_server");
  FakeXdsBootstrap::FakeAuthority authority;
  authority.set_server(authority_server);
  InitXdsClient(
      FakeXdsBootstrap::Builder().AddAuthority(kAuthority, authority));
  // Start a watch for "foo1".
  auto watcher = StartFooWatch("foo1");
  // Watcher should initially not see any resource reported.
  EXPECT_FALSE(watcher->HasEvent());
  // XdsClient should have created an ADS stream to the top-level xDS server.
  auto stream = WaitForAdsStream(*xds_client_->bootstrap().servers().front());
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
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  // Check metric data.
  EXPECT_TRUE(metrics_reporter_->WaitForMetricsReporterData(
      ::testing::ElementsAre(::testing::Pair(
          ::testing::Pair(kDefaultXdsServerUrl,
                          XdsFooResourceType::Get()->type_url()),
          1)),
      ::testing::ElementsAre(), ::testing::_));
  EXPECT_THAT(
      GetResourceCounts(),
      ::testing::ElementsAre(::testing::Pair(
          ResourceCountLabelsEq(XdsClient::kOldStyleAuthority,
                                XdsFooResourceType::Get()->type_url(), "acked"),
          1)));
  EXPECT_THAT(GetServerConnections(), ::testing::ElementsAre(::testing::Pair(
                                          kDefaultXdsServerUrl, true)));
  // Check CSDS data.
  ClientConfig csds = DumpCsds();
  EXPECT_THAT(csds.generic_xds_configs(),
              ::testing::UnorderedElementsAre(CsdsResourceAcked(
                  XdsFooResourceType::Get()->type_url(), "foo1",
                  resource->AsJsonString(), "1", TimestampProtoEq(kTime0))));
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  // Start a watch for the xdstp resource name.
  auto watcher2 = StartFooWatch(kXdstpResourceName);
  // Check metric data.
  EXPECT_THAT(
      GetServerConnections(),
      ::testing::ElementsAre(
          ::testing::Pair(kDefaultXdsServerUrl, true),
          ::testing::Pair(authority_server.target()->server_uri(), true)));
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
  // We increment time to make sure that the CSDS data gets a new timestamp.
  time_cache_.TestOnlySetNow(kTime1);
  stream2->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("2")
          .set_nonce("B")
          .AddFooResource(XdsFooResource(kXdstpResourceName, 3))
          .Serialize());
  // XdsClient should have delivered the response to the watcher.
  auto resource2 = watcher2->WaitForNextResource();
  ASSERT_NE(resource2, nullptr);
  EXPECT_EQ(resource2->name, kXdstpResourceName);
  EXPECT_EQ(resource2->value, 3);
  // Check metric data.
  EXPECT_TRUE(metrics_reporter_->WaitForMetricsReporterData(
      ::testing::ElementsAre(
          ::testing::Pair(
              ::testing::Pair(kDefaultXdsServerUrl,
                              XdsFooResourceType::Get()->type_url()),
              1),
          ::testing::Pair(
              ::testing::Pair(authority_server.target()->server_uri(),
                              XdsFooResourceType::Get()->type_url()),
              1)),
      ::testing::ElementsAre(), ::testing::_));
  EXPECT_THAT(
      GetResourceCounts(),
      ::testing::ElementsAre(
          ::testing::Pair(ResourceCountLabelsEq(
                              XdsClient::kOldStyleAuthority,
                              XdsFooResourceType::Get()->type_url(), "acked"),
                          1),
          ::testing::Pair(
              ResourceCountLabelsEq(
                  kAuthority, XdsFooResourceType::Get()->type_url(), "acked"),
              1)));
  EXPECT_THAT(
      GetServerConnections(),
      ::testing::ElementsAre(
          ::testing::Pair(kDefaultXdsServerUrl, true),
          ::testing::Pair(authority_server.target()->server_uri(), true)));
  // Check CSDS data.
  csds = DumpCsds();
  EXPECT_THAT(
      csds.generic_xds_configs(),
      ::testing::UnorderedElementsAre(
          CsdsResourceAcked(XdsFooResourceType::Get()->type_url(), "foo1",
                            resource->AsJsonString(), "1",
                            TimestampProtoEq(kTime0)),
          CsdsResourceAcked(XdsFooResourceType::Get()->type_url(),
                            kXdstpResourceName, resource2->AsJsonString(), "2",
                            TimestampProtoEq(kTime1))));
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
  auto error = watcher2->WaitForNextAmbientError();
  ASSERT_TRUE(error.has_value());
  EXPECT_EQ(error->code(), absl::StatusCode::kUnavailable);
  EXPECT_EQ(error->message(),
            "xDS channel for server other_xds_server: connection failed "
            "(node ID:xds_client_test)")
      << *error;
  // The watcher for "foo1" should not see any error.
  EXPECT_FALSE(watcher->HasEvent());
  // Check metric data.
  EXPECT_THAT(
      GetServerConnections(),
      ::testing::ElementsAre(
          ::testing::Pair(kDefaultXdsServerUrl, true),
          ::testing::Pair(authority_server.target()->server_uri(), false)));
  EXPECT_TRUE(metrics_reporter_->WaitForMetricsReporterData(
      ::testing::_, ::testing::_,
      ::testing::ElementsAre(
          ::testing::Pair(authority_server.target()->server_uri(), 1))));
  // Cancel watch for "foo1".
  CancelFooWatch(watcher.get(), "foo1");
  EXPECT_TRUE(stream->IsOrphaned());
  // Now cancel watch for xdstp resource name.
  CancelFooWatch(watcher2.get(), kXdstpResourceName);
  EXPECT_TRUE(stream2->IsOrphaned());
}

TEST_F(XdsClientTest, AdsReadWaitsForHandleRelease) {
  InitXdsClient();
  // Start watches for "foo1" and "foo2".
  auto watcher1 = StartFooWatch("foo1");
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
  auto watcher2 = StartFooWatch("foo2");
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1", "foo2"});
  // Send a response with 2 resources.
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("1")
          .set_nonce("A")
          .AddFooResource(XdsFooResource("foo1", 6))
          .AddFooResource(XdsFooResource("foo2", 10))
          .Serialize());
  // Send a response with a single resource, will not be read until the handle
  // is released
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("2")
          .set_nonce("B")
          .AddFooResource(XdsFooResource("foo1", 8))
          .Serialize());
  // XdsClient should have delivered the response to the watcher.
  auto resource1 = watcher1->WaitForNextResourceAndHandle();
  ASSERT_NE(resource1, std::nullopt);
  EXPECT_EQ(resource1->resource->name, "foo1");
  EXPECT_EQ(resource1->resource->value, 6);
  auto resource2 = watcher2->WaitForNextResourceAndHandle();
  ASSERT_NE(resource2, std::nullopt);
  EXPECT_EQ(resource2->resource->name, "foo2");
  EXPECT_EQ(resource2->resource->value, 10);
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1", "foo2"});
  EXPECT_TRUE(stream->WaitForReadsStarted(1));
  resource1->read_delay_handle.reset();
  EXPECT_TRUE(stream->WaitForReadsStarted(1));
  resource2->read_delay_handle.reset();
  EXPECT_TRUE(stream->WaitForReadsStarted(2));
  resource1 = watcher1->WaitForNextResourceAndHandle();
  ASSERT_NE(resource1, std::nullopt);
  EXPECT_EQ(resource1->resource->name, "foo1");
  EXPECT_EQ(resource1->resource->value, 8);
  EXPECT_EQ(watcher2->WaitForNextResourceAndHandle(), std::nullopt);
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"2", /*response_nonce=*/"B",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1", "foo2"});
  EXPECT_TRUE(stream->WaitForReadsStarted(2));
  resource1->read_delay_handle.reset();
  EXPECT_TRUE(stream->WaitForReadsStarted(3));
  // Cancel watch.
  CancelFooWatch(watcher1.get(), "foo1");
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"2", /*response_nonce=*/"B",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo2"});
  CancelFooWatch(watcher2.get(), "foo2");
  EXPECT_TRUE(stream->IsOrphaned());
}

TEST_F(XdsClientTest, FallbackAndRecover) {
  FakeXdsBootstrap::FakeXdsServer primary_server(kDefaultXdsServerUrl);
  FakeXdsBootstrap::FakeXdsServer fallback_server("fallback_xds_server");
  // Regular operation
  InitXdsClient(FakeXdsBootstrap::Builder().SetServers(
      {primary_server, fallback_server}));
  // Start a watch for "foo1".
  auto watcher = StartFooWatch("foo1");
  EXPECT_THAT(GetServerConnections(), ::testing::ElementsAre(::testing::Pair(
                                          kDefaultXdsServerUrl, true)));
  EXPECT_THAT(GetResourceCounts(),
              ::testing::ElementsAre(::testing::Pair(
                  ResourceCountLabelsEq(XdsClient::kOldStyleAuthority,
                                        XdsFooResourceType::Get()->type_url(),
                                        "requested"),
                  1)));
  EXPECT_TRUE(metrics_reporter_->WaitForMetricsReporterData(
      ::testing::IsEmpty(), ::testing::_, ::testing::ElementsAre()));
  // CSDS should show that the resource has been requested.
  ClientConfig csds = DumpCsds();
  EXPECT_THAT(csds.generic_xds_configs(),
              ::testing::ElementsAre(CsdsResourceRequested(
                  XdsFooResourceType::Get()->type_url(), "foo1")));
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
  // Input: Get initial response from primary server.
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("20")
          .set_nonce("O")
          .AddFooResource(XdsFooResource("foo1", 6))
          .Serialize());
  // Result (local): Resource is delivered to watcher.
  auto resource = watcher->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  // Result (local): Metrics show 1 resource update and 1 cached resource.
  EXPECT_TRUE(metrics_reporter_->WaitForMetricsReporterData(
      ::testing::ElementsAre(::testing::Pair(
          ::testing::Pair(kDefaultXdsServerUrl,
                          XdsFooResourceType::Get()->type_url()),
          1)),
      ::testing::_, ::testing::_));
  EXPECT_THAT(
      GetResourceCounts(),
      ::testing::ElementsAre(::testing::Pair(
          ResourceCountLabelsEq(XdsClient::kOldStyleAuthority,
                                XdsFooResourceType::Get()->type_url(), "acked"),
          1)));
  // Check CSDS data.
  csds = DumpCsds();
  EXPECT_THAT(csds.generic_xds_configs(),
              ::testing::UnorderedElementsAre(CsdsResourceAcked(
                  XdsFooResourceType::Get()->type_url(), "foo1",
                  resource->AsJsonString(), "20", TimestampProtoEq(kTime0))));
  // Result (remote): Client sends ACK to server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"20", /*response_nonce=*/"O",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  // Input: Trigger connection failure to primary.
  TriggerConnectionFailure(primary_server,
                           absl::UnavailableError("Server down"));
  // Result (local): The error is reported to the watcher.
  auto error = watcher->WaitForNextAmbientError();
  ASSERT_TRUE(error.has_value());
  EXPECT_EQ(error->code(), absl::StatusCode::kUnavailable);
  EXPECT_EQ(error->message(),
            "xDS channel for server default_xds_server: Server down (node "
            "ID:xds_client_test)");
  // Result (local): The metrics show the channel as being unhealthy.
  EXPECT_THAT(GetServerConnections(), ::testing::ElementsAre(::testing::Pair(
                                          kDefaultXdsServerUrl, false)));
  EXPECT_TRUE(metrics_reporter_->WaitForMetricsReporterData(
      ::testing::_, ::testing::_,
      ::testing::ElementsAre(::testing::Pair(kDefaultXdsServerUrl, 1))));
  // Input: Trigger stream failure.
  stream->MaybeSendStatusToClient(absl::UnavailableError("Stream failure"));
  // Result (local): The metrics still show the channel as being unhealthy.
  EXPECT_THAT(GetServerConnections(), ::testing::ElementsAre(::testing::Pair(
                                          kDefaultXdsServerUrl, false)));
  EXPECT_TRUE(metrics_reporter_->WaitForMetricsReporterData(
      ::testing::_, ::testing::_,
      ::testing::ElementsAre(::testing::Pair(kDefaultXdsServerUrl, 1))));
  // Result (remote): The client starts a new stream and sends a subscription
  //   message. Note that the server does not respond, so the channel will still
  //   have non-OK status.
  stream = WaitForAdsStream();
  ASSERT_NE(stream, nullptr);
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"20", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  // Input: Start second watch for foo1 (already cached).
  auto watcher_cached = StartFooWatch("foo1");
  // Result (local): New watcher gets the cached resource.
  resource = watcher_cached->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  // Result (local): New watcher gets the error from the channel state.
  error = watcher_cached->WaitForNextAmbientError();
  ASSERT_TRUE(error.has_value());
  EXPECT_EQ(error->message(),
            "xDS channel for server default_xds_server: Server down (node "
            "ID:xds_client_test)")
      << error->message();
  CancelFooWatch(watcher_cached.get(), "foo1");
  // Input: Start watch for foo2 (not already cached).
  auto watcher2 = StartFooWatch("foo2");
  // Result (local): Metrics show a healthy channel to the fallback server.
  EXPECT_THAT(
      GetServerConnections(),
      ::testing::ElementsAre(
          ::testing::Pair(kDefaultXdsServerUrl, false),
          ::testing::Pair(fallback_server.target()->server_uri(), true)));
  // Result (remote): Client sent a new request for both resources on the
  //   stream to the primary.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"20", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1", "foo2"});
  // Result (remote): Client created a stream to the fallback server and sent a
  //   request on that stream for both resources.
  auto stream2 = WaitForAdsStream(fallback_server);
  ASSERT_TRUE(stream2 != nullptr);
  request = WaitForRequest(stream2.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1", "foo2"});
  // Input: Fallback server sends a response with both resources.
  // We increment time to make sure that the CSDS data gets a new timestamp.
  time_cache_.TestOnlySetNow(kTime1);
  stream2->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("5")
          .set_nonce("A")
          .AddFooResource(XdsFooResource("foo1", 20))
          .AddFooResource(XdsFooResource("foo2", 30))
          .Serialize());
  // Result (local): Resources are delivered to watchers.
  resource = watcher->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 20);
  auto resource2 = watcher2->WaitForNextResource();
  ASSERT_NE(resource2, nullptr);
  EXPECT_EQ(resource2->name, "foo2");
  EXPECT_EQ(resource2->value, 30);
  // Result (local): Metrics show an update from fallback server.
  EXPECT_TRUE(metrics_reporter_->WaitForMetricsReporterData(
      ::testing::ElementsAre(
          ::testing::Pair(
              ::testing::Pair(kDefaultXdsServerUrl,
                              XdsFooResourceType::Get()->type_url()),
              1),
          ::testing::Pair(
              ::testing::Pair(fallback_server.target()->server_uri(),
                              XdsFooResourceType::Get()->type_url()),
              2)),
      ::testing::_, ::testing::_));
  EXPECT_THAT(
      GetServerConnections(),
      ::testing::ElementsAre(
          ::testing::Pair(kDefaultXdsServerUrl, false),
          ::testing::Pair(fallback_server.target()->server_uri(), true)));
  EXPECT_THAT(
      GetResourceCounts(),
      ::testing::ElementsAre(::testing::Pair(
          ResourceCountLabelsEq(XdsClient::kOldStyleAuthority,
                                XdsFooResourceType::Get()->type_url(), "acked"),
          2)));
  // Check CSDS data.
  csds = DumpCsds();
  EXPECT_THAT(csds.generic_xds_configs(),
              ::testing::UnorderedElementsAre(
                  CsdsResourceAcked(XdsFooResourceType::Get()->type_url(),
                                    "foo1", resource->AsJsonString(), "5",
                                    TimestampProtoEq(kTime1)),
                  CsdsResourceAcked(XdsFooResourceType::Get()->type_url(),
                                    "foo2", resource2->AsJsonString(), "5",
                                    TimestampProtoEq(kTime1))));
  // Result (remote): Client sends ACK to fallback server.
  request = WaitForRequest(stream2.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"5", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1", "foo2"});
  // Input: Primary server sends a response containing both resources.
  // We increment time to make sure that the CSDS data gets a new timestamp.
  time_cache_.TestOnlySetNow(kTime2);
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("15")
          .set_nonce("B")
          .AddFooResource(XdsFooResource("foo1", 35))
          .AddFooResource(XdsFooResource("foo2", 25))
          .Serialize());
  // Result (local): Resources are delivered to watchers.
  resource = watcher->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 35);
  resource2 = watcher2->WaitForNextResource();
  ASSERT_NE(resource2, nullptr);
  EXPECT_EQ(resource2->name, "foo2");
  EXPECT_EQ(resource2->value, 25);
  // Result (local): Metrics show that we've closed the channel to the fallback
  //   server and received resource updates from the primary server.
  EXPECT_TRUE(metrics_reporter_->WaitForMetricsReporterData(
      ::testing::ElementsAre(
          ::testing::Pair(
              ::testing::Pair(kDefaultXdsServerUrl,
                              XdsFooResourceType::Get()->type_url()),
              3),
          ::testing::Pair(
              ::testing::Pair(fallback_server.target()->server_uri(),
                              XdsFooResourceType::Get()->type_url()),
              2)),
      ::testing::_,
      ::testing::ElementsAre(::testing::Pair(kDefaultXdsServerUrl, 1))));
  EXPECT_THAT(GetServerConnections(), ::testing::ElementsAre(::testing::Pair(
                                          kDefaultXdsServerUrl, true)));
  // Check CSDS data.
  csds = DumpCsds();
  EXPECT_THAT(csds.generic_xds_configs(),
              ::testing::UnorderedElementsAre(
                  CsdsResourceAcked(XdsFooResourceType::Get()->type_url(),
                                    "foo1", resource->AsJsonString(), "15",
                                    TimestampProtoEq(kTime2)),
                  CsdsResourceAcked(XdsFooResourceType::Get()->type_url(),
                                    "foo2", resource2->AsJsonString(), "15",
                                    TimestampProtoEq(kTime2))));
  // Result (remote): The stream to the fallback server has been orphaned.
  EXPECT_TRUE(stream2->IsOrphaned());
  // Result (remote): Client sends ACK to server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"15", /*response_nonce=*/"B",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1", "foo2"});
  // Clean up.
  CancelFooWatch(watcher.get(), "foo1", /*delay_unsubscription=*/true);
  CancelFooWatch(watcher2.get(), "foo2");
  // Result (remote): The stream to the primary server has been orphaned.
  EXPECT_TRUE(stream->IsOrphaned());
}

// Test for both servers being unavailable
TEST_F(XdsClientTest, FallbackReportsError) {
  FakeXdsBootstrap::FakeXdsServer primary_server(kDefaultXdsServerUrl);
  FakeXdsBootstrap::FakeXdsServer fallback_server("fallback_xds_server");
  InitXdsClient(FakeXdsBootstrap::Builder().SetServers(
      {primary_server, fallback_server}));
  auto watcher = StartFooWatch("foo1");
  EXPECT_THAT(GetServerConnections(), ::testing::ElementsAre(::testing::Pair(
                                          kDefaultXdsServerUrl, true)));
  auto stream = WaitForAdsStream();
  ASSERT_TRUE(stream != nullptr);
  // XdsClient should have sent a subscription request on the ADS stream.
  auto request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  EXPECT_THAT(GetResourceCounts(),
              ::testing::ElementsAre(::testing::Pair(
                  ResourceCountLabelsEq(XdsClient::kOldStyleAuthority,
                                        XdsFooResourceType::Get()->type_url(),
                                        "requested"),
                  1)));
  TriggerConnectionFailure(primary_server,
                           absl::UnavailableError("Server down"));
  EXPECT_THAT(
      GetServerConnections(),
      ::testing::ElementsAre(
          ::testing::Pair(kDefaultXdsServerUrl, false),
          ::testing::Pair(fallback_server.target()->server_uri(), true)));
  EXPECT_TRUE(metrics_reporter_->WaitForMetricsReporterData(
      ::testing::_, ::testing::_,
      ::testing::ElementsAre(::testing::Pair(kDefaultXdsServerUrl, 1))));
  // CSDS should show that the resource has been requested.
  ClientConfig csds = DumpCsds();
  EXPECT_THAT(csds.generic_xds_configs(),
              ::testing::ElementsAre(CsdsResourceRequested(
                  XdsFooResourceType::Get()->type_url(), "foo1")));
  // Fallback happens now
  stream = WaitForAdsStream(fallback_server);
  ASSERT_NE(stream, nullptr);
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  TriggerConnectionFailure(fallback_server,
                           absl::UnavailableError("Another server down"));
  EXPECT_THAT(
      GetServerConnections(),
      ::testing::ElementsAre(
          ::testing::Pair(kDefaultXdsServerUrl, false),
          ::testing::Pair(fallback_server.target()->server_uri(), false)));
  EXPECT_TRUE(metrics_reporter_->WaitForMetricsReporterData(
      ::testing::_, ::testing::_,
      ::testing::ElementsAre(
          ::testing::Pair(kDefaultXdsServerUrl, 1),
          ::testing::Pair(fallback_server.target()->server_uri(), 1))));
  csds = DumpCsds();
  EXPECT_THAT(csds.generic_xds_configs(),
              ::testing::ElementsAre(CsdsResourceRequested(
                  XdsFooResourceType::Get()->type_url(), "foo1")));
  auto error = watcher->WaitForNextError();
  ASSERT_TRUE(error.has_value());
  EXPECT_THAT(error->code(), absl::StatusCode::kUnavailable);
  EXPECT_EQ(error->message(),
            "xDS channel for server fallback_xds_server: Another server down "
            "(node ID:xds_client_test)")
      << error->message();
}

TEST_F(XdsClientTest, FallbackOnStartup) {
  FakeXdsBootstrap::FakeXdsServer primary_server;
  FakeXdsBootstrap::FakeXdsServer fallback_server("fallback_xds_server");
  // Regular operation
  InitXdsClient(FakeXdsBootstrap::Builder().SetServers(
      {primary_server, fallback_server}));
  // Start a watch for "foo1".
  auto watcher = StartFooWatch("foo1");
  auto primary_stream = WaitForAdsStream(primary_server);
  ASSERT_NE(primary_stream, nullptr);
  // XdsClient should have sent a subscription request on the ADS stream.
  auto request = WaitForRequest(primary_stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  TriggerConnectionFailure(primary_server,
                           absl::UnavailableError("Primary server is down"));
  // XdsClient should have created an ADS stream.
  auto fallback_stream = WaitForAdsStream(fallback_server);
  ASSERT_NE(fallback_stream, nullptr);
  // XdsClient should have sent a subscription request on the ADS stream.
  request = WaitForRequest(fallback_stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  // Send a response.
  fallback_stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("1")
          .set_nonce("A")
          .AddFooResource(XdsFooResource("foo1", 6))
          .Serialize());
  EXPECT_THAT(
      GetServerConnections(),
      ::testing::ElementsAre(
          ::testing::Pair(kDefaultXdsServerUrl, false),
          ::testing::Pair(fallback_server.target()->server_uri(), true)));
  EXPECT_TRUE(metrics_reporter_->WaitForMetricsReporterData(
      ::testing::_, ::testing::_,
      ::testing::ElementsAre(::testing::Pair(kDefaultXdsServerUrl, 1))));
  // XdsClient should have delivered the response to the watcher.
  auto resource = watcher->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  // Client sends an ACK.
  request = WaitForRequest(fallback_stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  // Recover to primary
  primary_stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("5")
          .set_nonce("D")
          .AddFooResource(XdsFooResource("foo1", 42))
          .Serialize());
  EXPECT_TRUE(fallback_stream->IsOrphaned());
  resource = watcher->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 42);
  EXPECT_THAT(GetServerConnections(), ::testing::ElementsAre(::testing::Pair(
                                          kDefaultXdsServerUrl, true)));
  request = WaitForRequest(primary_stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"5", /*response_nonce=*/"D",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  return RUN_ALL_TESTS();
}
