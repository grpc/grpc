// Copyright 2017 gRPC authors.
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

#include "test/cpp/end2end/xds/xds_utils.h"

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "absl/memory/memory.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

#include <grpcpp/security/tls_certificate_provider.h>

#include "src/core/ext/filters/http/server/http_server_filter.h"
#include "src/core/ext/xds/xds_channel_args.h"
#include "src/core/ext/xds/xds_client_grpc.h"
#include "src/core/lib/gpr/tmpfile.h"
#include "src/core/lib/gprpp/env.h"
#include "src/core/lib/surface/server.h"
#include "src/cpp/client/secure_credentials.h"
#include "src/proto/grpc/testing/xds/v3/router.grpc.pb.h"
#include "test/core/util/resolve_localhost_ip46.h"

namespace grpc {
namespace testing {

using ::envoy::config::cluster::v3::Cluster;
using ::envoy::config::core::v3::HealthStatus;
using ::envoy::config::endpoint::v3::ClusterLoadAssignment;
using ::envoy::config::listener::v3::Listener;
using ::envoy::config::route::v3::RouteConfiguration;
using ::envoy::extensions::filters::network::http_connection_manager::v3::
    HttpConnectionManager;

//
// XdsBootstrapBuilder
//

std::string XdsBootstrapBuilder::Build() {
  std::vector<std::string> fields;
  fields.push_back(MakeXdsServersText(top_server_));
  if (!client_default_listener_resource_name_template_.empty()) {
    fields.push_back(
        absl::StrCat("  \"client_default_listener_resource_name_template\": \"",
                     client_default_listener_resource_name_template_, "\""));
  }
  fields.push_back(MakeNodeText());
  if (!server_listener_resource_name_template_.empty()) {
    fields.push_back(
        absl::StrCat("  \"server_listener_resource_name_template\": \"",
                     server_listener_resource_name_template_, "\""));
  }
  fields.push_back(MakeCertificateProviderText());
  fields.push_back(MakeAuthorityText());
  return absl::StrCat("{", absl::StrJoin(fields, ",\n"), "}");
}

std::string XdsBootstrapBuilder::MakeXdsServersText(
    absl::string_view server_uri) {
  constexpr char kXdsServerTemplate[] =
      "      \"xds_servers\": [\n"
      "        {\n"
      "          \"server_uri\": \"<SERVER_URI>\",\n"
      "          \"channel_creds\": [\n"
      "            {\n"
      "              \"type\": \"<SERVER_CREDS_TYPE>\"\n"
      "            }\n"
      "          ],\n"
      "          \"server_features\": [<SERVER_FEATURES>]\n"
      "        }\n"
      "      ]";
  std::vector<std::string> server_features;
  if (ignore_resource_deletion_) {
    server_features.push_back("\"ignore_resource_deletion\"");
  }
  return absl::StrReplaceAll(
      kXdsServerTemplate,
      {{"<SERVER_URI>", server_uri},
       {"<SERVER_CREDS_TYPE>", xds_channel_creds_type_},
       {"<SERVER_FEATURES>", absl::StrJoin(server_features, ", ")}});
}

std::string XdsBootstrapBuilder::MakeNodeText() {
  constexpr char kXdsNode[] =
      "  \"node\": {\n"
      "    \"id\": \"xds_end2end_test\",\n"
      "    \"cluster\": \"test\",\n"
      "    \"metadata\": {\n"
      "      \"foo\": \"bar\"\n"
      "    },\n"
      "    \"locality\": {\n"
      "      \"region\": \"corp\",\n"
      "      \"zone\": \"svl\",\n"
      "      \"sub_zone\": \"mp3\"\n"
      "    }\n"
      "  }";
  return kXdsNode;
}

std::string XdsBootstrapBuilder::MakeCertificateProviderText() {
  std::vector<std::string> entries;
  for (const auto& p : plugins_) {
    const std::string& key = p.first;
    const PluginInfo& plugin_info = p.second;
    std::vector<std::string> fields;
    fields.push_back(absl::StrFormat("    \"%s\": {", key));
    if (!plugin_info.plugin_config.empty()) {
      fields.push_back(
          absl::StrFormat("      \"plugin_name\": \"%s\",", plugin_info.name));
      fields.push_back(absl::StrCat("      \"config\": {\n",
                                    plugin_info.plugin_config, "\n      }"));
    } else {
      fields.push_back(
          absl::StrFormat("      \"plugin_name\": \"%s\"", plugin_info.name));
    }
    fields.push_back("    }");
    entries.push_back(absl::StrJoin(fields, "\n"));
  }
  return absl::StrCat("  \"certificate_providers\": {\n",
                      absl::StrJoin(entries, ",\n"), "  \n}");
}

std::string XdsBootstrapBuilder::MakeAuthorityText() {
  std::vector<std::string> entries;
  for (const auto& p : authorities_) {
    const std::string& name = p.first;
    const AuthorityInfo& authority_info = p.second;
    std::vector<std::string> fields = {
        MakeXdsServersText(authority_info.server)};
    if (!authority_info.client_listener_resource_name_template.empty()) {
      fields.push_back(absl::StrCat(
          "\"client_listener_resource_name_template\": \"",
          authority_info.client_listener_resource_name_template, "\""));
    }
    entries.push_back(absl::StrCat(absl::StrFormat("\"%s\": {\n  ", name),
                                   absl::StrJoin(fields, ",\n"), "\n}"));
  }
  return absl::StrCat("\"authorities\": {\n", absl::StrJoin(entries, ",\n"),
                      "\n}");
}

//
// XdsResourceUtils::ClientHcmAccessor
//

HttpConnectionManager XdsResourceUtils::ClientHcmAccessor::Unpack(
    const Listener& listener) const {
  HttpConnectionManager http_connection_manager;
  listener.api_listener().api_listener().UnpackTo(&http_connection_manager);
  return http_connection_manager;
}

void XdsResourceUtils::ClientHcmAccessor::Pack(const HttpConnectionManager& hcm,
                                               Listener* listener) const {
  auto* api_listener = listener->mutable_api_listener()->mutable_api_listener();
  api_listener->PackFrom(hcm);
}

//
// XdsResourceUtils::ServerHcmAccessor
//

HttpConnectionManager XdsResourceUtils::ServerHcmAccessor::Unpack(
    const Listener& listener) const {
  HttpConnectionManager http_connection_manager;
  listener.default_filter_chain().filters().at(0).typed_config().UnpackTo(
      &http_connection_manager);
  return http_connection_manager;
}

void XdsResourceUtils::ServerHcmAccessor::Pack(const HttpConnectionManager& hcm,
                                               Listener* listener) const {
  auto* filters = listener->mutable_default_filter_chain()->mutable_filters();
  if (filters->empty()) filters->Add();
  filters->at(0).mutable_typed_config()->PackFrom(hcm);
}

//
// XdsResourceUtils
//

const char XdsResourceUtils::kDefaultLocalityRegion[] =
    "xds_default_locality_region";
const char XdsResourceUtils::kDefaultLocalityZone[] =
    "xds_default_locality_zone";

const char XdsResourceUtils::kServerName[] = "server.example.com";
const char XdsResourceUtils::kDefaultRouteConfigurationName[] =
    "route_config_name";
const char XdsResourceUtils::kDefaultClusterName[] = "cluster_name";
const char XdsResourceUtils::kDefaultEdsServiceName[] = "eds_service_name";
const char XdsResourceUtils::kDefaultServerRouteConfigurationName[] =
    "default_server_route_config_name";

Listener XdsResourceUtils::DefaultListener() {
  Listener listener;
  listener.set_name(kServerName);
  ClientHcmAccessor().Pack(DefaultHcm(), &listener);
  return listener;
}

RouteConfiguration XdsResourceUtils::DefaultRouteConfig() {
  RouteConfiguration route_config;
  route_config.set_name(kDefaultRouteConfigurationName);
  auto* virtual_host = route_config.add_virtual_hosts();
  virtual_host->add_domains("*");
  auto* route = virtual_host->add_routes();
  route->mutable_match()->set_prefix("");
  route->mutable_route()->set_cluster(kDefaultClusterName);
  return route_config;
}

Cluster XdsResourceUtils::DefaultCluster() {
  Cluster cluster;
  cluster.set_name(kDefaultClusterName);
  cluster.set_type(Cluster::EDS);
  auto* eds_config = cluster.mutable_eds_cluster_config();
  eds_config->mutable_eds_config()->mutable_self();
  eds_config->set_service_name(kDefaultEdsServiceName);
  cluster.set_lb_policy(Cluster::ROUND_ROBIN);
  return cluster;
}

Listener XdsResourceUtils::DefaultServerListener() {
  Listener listener;
  listener.mutable_address()->mutable_socket_address()->set_address(
      grpc_core::LocalIp());
  ServerHcmAccessor().Pack(DefaultHcm(), &listener);
  return listener;
}

RouteConfiguration XdsResourceUtils::DefaultServerRouteConfig() {
  RouteConfiguration route_config;
  route_config.set_name(kDefaultServerRouteConfigurationName);
  auto* virtual_host = route_config.add_virtual_hosts();
  virtual_host->add_domains("*");
  auto* route = virtual_host->add_routes();
  route->mutable_match()->set_prefix("");
  route->mutable_non_forwarding_action();
  return route_config;
}

HttpConnectionManager XdsResourceUtils::DefaultHcm() {
  HttpConnectionManager http_connection_manager;
  auto* filter = http_connection_manager.add_http_filters();
  filter->set_name("router");
  filter->mutable_typed_config()->PackFrom(
      envoy::extensions::filters::http::router::v3::Router());
  return http_connection_manager;
}

std::string XdsResourceUtils::GetServerListenerName(int port) {
  return absl::StrCat("grpc/server?xds.resource.listening_address=",
                      grpc_core::LocalIp(), ":", port);
}

Listener XdsResourceUtils::PopulateServerListenerNameAndPort(
    const Listener& listener_template, int port) {
  Listener listener = listener_template;
  listener.set_name(GetServerListenerName(port));
  listener.mutable_address()->mutable_socket_address()->set_port_value(port);
  return listener;
}

void XdsResourceUtils::SetListenerAndRouteConfiguration(
    AdsServiceImpl* ads_service, Listener listener,
    const RouteConfiguration& route_config, bool use_rds,
    const HcmAccessor& hcm_accessor) {
  HttpConnectionManager http_connection_manager = hcm_accessor.Unpack(listener);
  if (use_rds) {
    auto* rds = http_connection_manager.mutable_rds();
    rds->set_route_config_name(route_config.name());
    rds->mutable_config_source()->mutable_self();
    ads_service->SetRdsResource(route_config);
  } else {
    *http_connection_manager.mutable_route_config() = route_config;
  }
  hcm_accessor.Pack(http_connection_manager, &listener);
  ads_service->SetLdsResource(listener);
}

void XdsResourceUtils::SetRouteConfiguration(
    AdsServiceImpl* ads_service, const RouteConfiguration& route_config,
    bool use_rds, const Listener* listener_to_copy) {
  if (use_rds) {
    ads_service->SetRdsResource(route_config);
  } else {
    Listener listener(listener_to_copy == nullptr ? DefaultListener()
                                                  : *listener_to_copy);
    HttpConnectionManager http_connection_manager =
        ClientHcmAccessor().Unpack(listener);
    *(http_connection_manager.mutable_route_config()) = route_config;
    ClientHcmAccessor().Pack(http_connection_manager, &listener);
    ads_service->SetLdsResource(listener);
  }
}

ClusterLoadAssignment XdsResourceUtils::BuildEdsResource(
    const EdsResourceArgs& args, absl::string_view eds_service_name) {
  ClusterLoadAssignment assignment;
  assignment.set_cluster_name(eds_service_name);
  for (const auto& locality : args.locality_list) {
    auto* endpoints = assignment.add_endpoints();
    endpoints->mutable_load_balancing_weight()->set_value(locality.lb_weight);
    endpoints->set_priority(locality.priority);
    endpoints->mutable_locality()->set_region(kDefaultLocalityRegion);
    endpoints->mutable_locality()->set_zone(kDefaultLocalityZone);
    endpoints->mutable_locality()->set_sub_zone(locality.sub_zone);
    for (size_t i = 0; i < locality.endpoints.size(); ++i) {
      const auto& endpoint = locality.endpoints[i];
      auto* lb_endpoints = endpoints->add_lb_endpoints();
      if (locality.endpoints.size() > i &&
          locality.endpoints[i].health_status != HealthStatus::UNKNOWN) {
        lb_endpoints->set_health_status(endpoint.health_status);
      }
      if (locality.endpoints.size() > i && endpoint.lb_weight >= 1) {
        lb_endpoints->mutable_load_balancing_weight()->set_value(
            endpoint.lb_weight);
      }
      auto* endpoint_proto = lb_endpoints->mutable_endpoint();
      auto* socket_address =
          endpoint_proto->mutable_address()->mutable_socket_address();
      socket_address->set_address(grpc_core::LocalIp());
      socket_address->set_port_value(endpoint.port);
      for (int port : endpoint.additional_ports) {
        socket_address = endpoint_proto->add_additional_addresses()
                             ->mutable_address()
                             ->mutable_socket_address();
        socket_address->set_address(grpc_core::LocalIp());
        socket_address->set_port_value(port);
      }
    }
  }
  if (!args.drop_categories.empty()) {
    auto* policy = assignment.mutable_policy();
    for (const auto& p : args.drop_categories) {
      const std::string& name = p.first;
      const uint32_t parts_per_million = p.second;
      auto* drop_overload = policy->add_drop_overloads();
      drop_overload->set_category(name);
      auto* drop_percentage = drop_overload->mutable_drop_percentage();
      drop_percentage->set_numerator(parts_per_million);
      drop_percentage->set_denominator(args.drop_denominator);
    }
  }
  return assignment;
}

}  // namespace testing
}  // namespace grpc
