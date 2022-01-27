//
// Copyright 2020 gRPC authors.
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

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_XDS_XDS_CHANNEL_ARGS_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_XDS_XDS_CHANNEL_ARGS_H

// Channel arg indicating the xDS cluster name.
// Set by xds_cluster_impl LB policy and used by GoogleDefaultCredentials.
#define GRPC_ARG_XDS_CLUSTER_NAME "grpc.internal.xds_cluster_name"

// For testing purpose, this channel arg indicating xds_cluster_resolver LB
// policy should use the fake DNS resolver to resolve logical dns cluster.
#define GRPC_ARG_XDS_LOGICAL_DNS_CLUSTER_FAKE_RESOLVER_RESPONSE_GENERATOR \
  "grpc.TEST_ONLY.xds_logical_dns_cluster_fake_resolver_response_generator"

#endif  // GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_XDS_XDS_CHANNEL_ARGS_H
