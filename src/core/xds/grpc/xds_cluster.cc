//
// Copyright 2018 gRPC authors.
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

#include "src/core/xds/grpc/xds_cluster.h"

#include <string>

#include "src/core/util/json/json_writer.h"
#include "src/core/util/match.h"
#include "src/core/util/string.h"
#include "src/core/util/time.h"
#include "src/core/xds/grpc/xds_common_types.h"

namespace grpc_core {

std::string XdsClusterResource::UpstreamTlsContext::ToString() const {
  std::string result = "{";
  bool is_first = true;
  if (!common_tls_context.Empty()) {
    StrAppend(result, "common_tls_context=");
    StrAppend(result, common_tls_context.ToString());
    is_first = false;
  }
  if (!is_first) StrAppend(result, ", ");
  StrAppend(result, "sni=");
  StrAppend(result, sni);
  if (auto_host_sni) {
    StrAppend(result, ", auto_host_sni=true");
  }
  if (auto_sni_san_validation) {
    StrAppend(result, ", auto_sni_san_validation=true");
  }
  StrAppend(result, "}");
  return result;
}

std::string XdsClusterResource::ToString() const {
  std::string result = "{";
  Match(
      type,
      [&](const Eds& eds) {
        StrAppend(result, "type=EDS");
        if (!eds.eds_service_name.empty()) {
          StrAppend(result, ", eds_service_name=");
          StrAppend(result, eds.eds_service_name);
        }
      },
      [&](const LogicalDns& logical_dns) {
        StrAppend(result, "type=LOGICAL_DNS, dns_hostname=");
        StrAppend(result, logical_dns.hostname);
      },
      [&](const Aggregate& aggregate) {
        StrAppend(result, "type=AGGREGATE, prioritized_cluster_names=[");
        bool is_first_inner = true;
        for (const auto& name : aggregate.prioritized_cluster_names) {
          if (!is_first_inner) StrAppend(result, ", ");
          StrAppend(result, name);
          is_first_inner = false;
        }
        StrAppend(result, "]");
      });
  StrAppend(result, ", lb_policy_config=");
  StrAppend(result, JsonDump(Json::FromArray(lb_policy_config)));
  if (lrs_load_reporting_server != nullptr) {
    StrAppend(result, ", lrs_load_reporting_server_name=");
    StrAppend(result, lrs_load_reporting_server->server_uri());
  }
  if (lrs_backend_metric_propagation != nullptr) {
    StrAppend(result, ", lrs_backend_metric_propagation=");
    StrAppend(result, lrs_backend_metric_propagation->AsString());
  }
  if (use_http_connect) StrAppend(result, ", use_http_connect=true");
  StrAppend(result, ", upstream_tls_context=");
  StrAppend(result, upstream_tls_context.ToString());
  if (connection_idle_timeout != Duration::Zero()) {
    StrAppend(result, ", connection_idle_timeout=");
    StrAppend(result, connection_idle_timeout.ToString());
  }
  StrAppend(result, ", max_concurrent_requests=");
  StrAppend(result, std::to_string(max_concurrent_requests));
  StrAppend(result, ", override_host_statuses=");
  StrAppend(result, override_host_statuses.ToString());
  if (!metadata.empty()) {
    StrAppend(result, ", metadata={");
    StrAppend(result, metadata.ToString());
    StrAppend(result, "}");
  }
  StrAppend(result, "}");
  return result;
}

}  // namespace grpc_core
