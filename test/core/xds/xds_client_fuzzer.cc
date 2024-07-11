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

#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "absl/types/optional.h"

#include <grpc/grpc.h>

#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/xds/grpc/xds_bootstrap_grpc.h"
#include "src/core/xds/grpc/xds_cluster.h"
#include "src/core/xds/grpc/xds_endpoint.h"
#include "src/core/xds/grpc/xds_listener.h"
#include "src/core/xds/grpc/xds_route_config.h"
#include "src/core/xds/xds_client/xds_bootstrap.h"
#include "src/core/xds/xds_client/xds_client.h"
#include "src/libfuzzer/libfuzzer_macro.h"
#include "src/proto/grpc/testing/xds/v3/discovery.pb.h"
#include "test/core/xds/xds_client_fuzzer.pb.h"
#include "test/core/xds/xds_client_test_peer.h"
#include "test/core/xds/xds_transport_fake.h"

namespace grpc_core {

class Fuzzer {
 public:
  explicit Fuzzer(absl::string_view bootstrap_json) {
    auto bootstrap = GrpcXdsBootstrap::Create(bootstrap_json);
    if (!bootstrap.ok()) {
      LOG(ERROR) << "error creating bootstrap: " << bootstrap.status();
      // Leave xds_client_ unset, so Act() will be a no-op.
      return;
    }
    auto transport_factory = MakeOrphanable<FakeXdsTransportFactory>(
        []() { Crash("Multiple concurrent reads"); });
    transport_factory->SetAutoCompleteMessagesFromClient(false);
    transport_factory->SetAbortOnUndrainedMessages(false);
    transport_factory_ = transport_factory.get();
    xds_client_ = MakeRefCounted<XdsClient>(
        std::move(*bootstrap), std::move(transport_factory),
        grpc_event_engine::experimental::GetDefaultEventEngine(),
        /*metrics_reporter=*/nullptr, "foo agent", "foo version");
  }

  void Act(const xds_client_fuzzer::Action& action) {
    if (xds_client_ == nullptr) return;
    switch (action.action_type_case()) {
      case xds_client_fuzzer::Action::kStartWatch:
        switch (action.start_watch().resource_type().resource_type_case()) {
          case xds_client_fuzzer::ResourceType::kListener:
            StartWatch(&listener_watchers_,
                       action.start_watch().resource_name());
            break;
          case xds_client_fuzzer::ResourceType::kRouteConfig:
            StartWatch(&route_config_watchers_,
                       action.start_watch().resource_name());
            break;
          case xds_client_fuzzer::ResourceType::kCluster:
            StartWatch(&cluster_watchers_,
                       action.start_watch().resource_name());
            break;
          case xds_client_fuzzer::ResourceType::kEndpoint:
            StartWatch(&endpoint_watchers_,
                       action.start_watch().resource_name());
            break;
          case xds_client_fuzzer::ResourceType::RESOURCE_TYPE_NOT_SET:
            break;
        }
        break;
      case xds_client_fuzzer::Action::kStopWatch:
        switch (action.stop_watch().resource_type().resource_type_case()) {
          case xds_client_fuzzer::ResourceType::kListener:
            StopWatch(&listener_watchers_, action.stop_watch().resource_name());
            break;
          case xds_client_fuzzer::ResourceType::kRouteConfig:
            StopWatch(&route_config_watchers_,
                      action.stop_watch().resource_name());
            break;
          case xds_client_fuzzer::ResourceType::kCluster:
            StopWatch(&cluster_watchers_, action.stop_watch().resource_name());
            break;
          case xds_client_fuzzer::ResourceType::kEndpoint:
            StopWatch(&endpoint_watchers_, action.stop_watch().resource_name());
            break;
          case xds_client_fuzzer::ResourceType::RESOURCE_TYPE_NOT_SET:
            break;
        }
        break;
      case xds_client_fuzzer::Action::kDumpCsdsData:
        testing::XdsClientTestPeer(xds_client_.get()).TestDumpClientConfig();
        break;
      case xds_client_fuzzer::Action::kReportResourceCounts:
        testing::XdsClientTestPeer(xds_client_.get())
            .TestReportResourceCounts(
                [](const testing::XdsClientTestPeer::ResourceCountLabels&
                       labels,
                   uint64_t count) {
                  LOG(INFO) << "xds_authority=\"" << labels.xds_authority
                            << "\", resource_type=\"" << labels.resource_type
                            << "\", cache_state=\"" << labels.cache_state
                            << "\" count=" << count;
                });
        break;
      case xds_client_fuzzer::Action::kReportServerConnections:
        testing::XdsClientTestPeer(xds_client_.get())
            .TestReportServerConnections(
                [](absl::string_view xds_server, bool connected) {
                  LOG(INFO) << "xds_server=\"" << xds_server
                            << "\" connected=" << connected;
                });
        break;
      case xds_client_fuzzer::Action::kTriggerConnectionFailure:
        TriggerConnectionFailure(
            action.trigger_connection_failure().authority(),
            ToAbslStatus(action.trigger_connection_failure().status()));
        break;
      case xds_client_fuzzer::Action::kReadMessageFromClient:
        ReadMessageFromClient(action.read_message_from_client().stream_id(),
                              action.read_message_from_client().ok());
        break;
      case xds_client_fuzzer::Action::kSendMessageToClient:
        SendMessageToClient(action.send_message_to_client().stream_id(),
                            action.send_message_to_client().response());
        break;
      case xds_client_fuzzer::Action::kSendStatusToClient:
        SendStatusToClient(
            action.send_status_to_client().stream_id(),
            ToAbslStatus(action.send_status_to_client().status()));
        break;
      case xds_client_fuzzer::Action::ACTION_TYPE_NOT_SET:
        break;
    }
  }

 private:
  template <typename ResourceTypeType>
  class Watcher : public ResourceTypeType::WatcherInterface {
   public:
    using ResourceType = ResourceTypeType;

    explicit Watcher(std::string resource_name)
        : resource_name_(std::move(resource_name)) {}

    void OnResourceChanged(
        std::shared_ptr<const typename ResourceType::ResourceType> resource,
        RefCountedPtr<XdsClient::ReadDelayHandle> /* read_delay_handle */)
        override {
      LOG(INFO) << "==> OnResourceChanged(" << ResourceType::Get()->type_url()
                << " " << resource_name_ << "): " << resource->ToString();
    }

    void OnError(
        absl::Status status,
        RefCountedPtr<XdsClient::ReadDelayHandle> /* read_delay_handle */)
        override {
      LOG(INFO) << "==> OnError(" << ResourceType::Get()->type_url() << " "
                << resource_name_ << "): " << status;
    }

    void OnResourceDoesNotExist(
        RefCountedPtr<XdsClient::ReadDelayHandle> /* read_delay_handle */)
        override {
      LOG(INFO) << "==> OnResourceDoesNotExist("
                << ResourceType::Get()->type_url() << " " << resource_name_
                << ")";
    }

   private:
    std::string resource_name_;
  };

  using ListenerWatcher = Watcher<XdsListenerResourceType>;
  using RouteConfigWatcher = Watcher<XdsRouteConfigResourceType>;
  using ClusterWatcher = Watcher<XdsClusterResourceType>;
  using EndpointWatcher = Watcher<XdsEndpointResourceType>;

  template <typename WatcherType>
  void StartWatch(std::map<std::string, std::set<WatcherType*>>* watchers,
                  std::string resource_name) {
    LOG(INFO) << "### StartWatch("
              << WatcherType::ResourceType::Get()->type_url() << " "
              << resource_name << ")";
    auto watcher = MakeRefCounted<WatcherType>(resource_name);
    (*watchers)[resource_name].insert(watcher.get());
    WatcherType::ResourceType::Get()->StartWatch(
        xds_client_.get(), resource_name, std::move(watcher));
  }

  template <typename WatcherType>
  void StopWatch(std::map<std::string, std::set<WatcherType*>>* watchers,
                 std::string resource_name) {
    LOG(INFO) << "### StopWatch("
              << WatcherType::ResourceType::Get()->type_url() << " "
              << resource_name << ")";
    auto& watchers_set = (*watchers)[resource_name];
    auto it = watchers_set.begin();
    if (it == watchers_set.end()) return;
    WatcherType::ResourceType::Get()->CancelWatch(xds_client_.get(),
                                                  resource_name, *it);
    watchers_set.erase(it);
  }

  static absl::Status ToAbslStatus(const xds_client_fuzzer::Status& status) {
    return absl::Status(static_cast<absl::StatusCode>(status.code()),
                        status.message());
  }

  const XdsBootstrap::XdsServer* GetServer(const std::string& authority) {
    const GrpcXdsBootstrap& bootstrap =
        static_cast<const GrpcXdsBootstrap&>(xds_client_->bootstrap());
    if (authority.empty()) return bootstrap.servers().front();
    const auto* authority_entry =
        static_cast<const GrpcXdsBootstrap::GrpcAuthority*>(
            bootstrap.LookupAuthority(authority));
    if (authority_entry == nullptr) return nullptr;
    if (!authority_entry->servers().empty()) {
      return authority_entry->servers().front();
    }
    return bootstrap.servers().front();
  }

  void TriggerConnectionFailure(const std::string& authority,
                                absl::Status status) {
    LOG(INFO) << "### TriggerConnectionFailure(" << authority
              << "): " << status;
    const auto* xds_server = GetServer(authority);
    if (xds_server == nullptr) return;
    transport_factory_->TriggerConnectionFailure(*xds_server,
                                                 std::move(status));
  }

  static const char* StreamIdMethod(
      const xds_client_fuzzer::StreamId& stream_id) {
    switch (stream_id.method_case()) {
      case xds_client_fuzzer::StreamId::kAds:
        return FakeXdsTransportFactory::kAdsMethod;
      case xds_client_fuzzer::StreamId::kLrs:
        return FakeXdsTransportFactory::kLrsMethod;
      case xds_client_fuzzer::StreamId::METHOD_NOT_SET:
        return nullptr;
    }
  }

  RefCountedPtr<FakeXdsTransportFactory::FakeStreamingCall> GetStream(
      const xds_client_fuzzer::StreamId& stream_id) {
    const auto* xds_server = GetServer(stream_id.authority());
    if (xds_server == nullptr) return nullptr;
    const char* method = StreamIdMethod(stream_id);
    if (method == nullptr) return nullptr;
    return transport_factory_->WaitForStream(*xds_server, method,
                                             absl::ZeroDuration());
  }

  static std::string StreamIdString(
      const xds_client_fuzzer::StreamId& stream_id) {
    return absl::StrCat("{authority=\"", stream_id.authority(),
                        "\", method=", StreamIdMethod(stream_id), "}");
  }

  void ReadMessageFromClient(const xds_client_fuzzer::StreamId& stream_id,
                             bool ok) {
    LOG(INFO) << "### ReadMessageFromClient(" << StreamIdString(stream_id)
              << "): " << (ok ? "true" : "false");
    auto stream = GetStream(stream_id);
    if (stream == nullptr) return;
    LOG(INFO) << "    stream=" << stream.get();
    auto message = stream->WaitForMessageFromClient(absl::ZeroDuration());
    if (message.has_value()) {
      LOG(INFO) << "    completing send_message";
      stream->CompleteSendMessageFromClient(ok);
    }
  }

  void SendMessageToClient(
      const xds_client_fuzzer::StreamId& stream_id,
      const envoy::service::discovery::v3::DiscoveryResponse& response) {
    LOG(INFO) << "### SendMessageToClient(" << StreamIdString(stream_id) << ")";
    auto stream = GetStream(stream_id);
    if (stream == nullptr) return;
    LOG(INFO) << "    stream=" << stream.get();
    stream->SendMessageToClient(response.SerializeAsString());
  }

  void SendStatusToClient(const xds_client_fuzzer::StreamId& stream_id,
                          absl::Status status) {
    LOG(INFO) << "### SendStatusToClient(" << StreamIdString(stream_id)
              << "): " << status;
    auto stream = GetStream(stream_id);
    if (stream == nullptr) return;
    LOG(INFO) << "    stream=" << stream.get();
    stream->MaybeSendStatusToClient(std::move(status));
  }

  RefCountedPtr<XdsClient> xds_client_;
  FakeXdsTransportFactory* transport_factory_;

  // Maps of currently active watchers for each resource type, keyed by
  // resource name.
  std::map<std::string, std::set<ListenerWatcher*>> listener_watchers_;
  std::map<std::string, std::set<RouteConfigWatcher*>> route_config_watchers_;
  std::map<std::string, std::set<ClusterWatcher*>> cluster_watchers_;
  std::map<std::string, std::set<EndpointWatcher*>> endpoint_watchers_;
};

}  // namespace grpc_core

bool squelch = true;

DEFINE_PROTO_FUZZER(const xds_client_fuzzer::Message& message) {
  grpc_init();
  grpc_core::Fuzzer fuzzer(message.bootstrap());
  for (int i = 0; i < message.actions_size(); i++) {
    fuzzer.Act(message.actions(i));
  }
  grpc_shutdown();
}
