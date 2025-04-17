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

#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "src/core/util/json/json_writer.h"
#include "src/core/util/match.h"
#include "src/core/util/time.h"
#include "src/core/xds/grpc/xds_common_types.h"

namespace grpc_core {

std::string XdsClusterResource::ToString() const {
  std::vector<std::string> contents;
  Match(
      type,
      [&](const Eds& eds) {
        contents.push_back("type=EDS");
        if (!eds.eds_service_name.empty()) {
          contents.push_back(
              absl::StrCat("eds_service_name=", eds.eds_service_name));
        }
      },
      [&](const LogicalDns& logical_dns) {
        contents.push_back("type=LOGICAL_DNS");
        contents.push_back(absl::StrCat("dns_hostname=", logical_dns.hostname));
      },
      [&](const Aggregate& aggregate) {
        contents.push_back("type=AGGREGATE");
        contents.push_back(absl::StrCat(
            "prioritized_cluster_names=[",
            absl::StrJoin(aggregate.prioritized_cluster_names, ", "), "]"));
      });
  contents.push_back(absl::StrCat("lb_policy_config=",
                                  JsonDump(Json::FromArray(lb_policy_config))));
  if (lrs_load_reporting_server != nullptr) {
    contents.push_back(absl::StrCat("lrs_load_reporting_server_name=",
                                    lrs_load_reporting_server->server_uri()));
  }
  if (lrs_backend_metric_propagation != nullptr) {
    contents.push_back(
        absl::StrCat("lrs_backend_metric_propagation=",
                     lrs_backend_metric_propagation->AsString()));
  }
  if (use_http_connect) contents.push_back("use_http_connect=true");
  if (!common_tls_context.Empty()) {
    contents.push_back(
        absl::StrCat("common_tls_context=", common_tls_context.ToString()));
  }
  if (connection_idle_timeout != Duration::Zero()) {
    contents.push_back(absl::StrCat("connection_idle_timeout=",
                                    connection_idle_timeout.ToString()));
  }
  contents.push_back(
      absl::StrCat("max_concurrent_requests=", max_concurrent_requests));
  contents.push_back(absl::StrCat("override_host_statuses=",
                                  override_host_statuses.ToString()));
  if (!metadata.empty()) {
    contents.push_back(absl::StrCat("metadata={", metadata.ToString(), "}"));
  }
  return absl::StrCat("{", absl::StrJoin(contents, ", "), "}");
}

}  // namespace grpc_core
