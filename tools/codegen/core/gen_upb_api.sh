#!/bin/bash

# Copyright 2016 gRPC authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

set -ex

cd $(dirname $0)/../../..
bazel=`pwd`/tools/bazel

if [ $# -eq 0 ]; then
  UPB_OUTPUT_DIR=$PWD/src/core/ext/upb-generated
  UPBDEFS_OUTPUT_DIR=$PWD/src/core/ext/upbdefs-generated
  rm -rf $UPB_OUTPUT_DIR
  rm -rf $UPBDEFS_OUTPUT_DIR
  mkdir -p $UPB_OUTPUT_DIR
  mkdir -p $UPBDEFS_OUTPUT_DIR
else
  UPB_OUTPUT_DIR=$1/upb-generated
  UPBDEFS_OUTPUT_DIR=$1/upbdefs-generated
  mkdir $UPB_OUTPUT_DIR
  mkdir $UPBDEFS_OUTPUT_DIR
fi

$bazel build @com_google_protobuf//:protoc
PROTOC=$PWD/bazel-bin/external/com_google_protobuf/protoc

$bazel build @upb//upbc:protoc-gen-upb
UPB_PLUGIN=$PWD/bazel-bin/external/upb/upbc/protoc-gen-upb

$bazel build @upb//upbc:protoc-gen-upbdefs
UPBDEFS_PLUGIN=$PWD/bazel-bin/external/upb/upbc/protoc-gen-upbdefs

proto_files=( \
  "envoy/admin/v3/config_dump.proto" \
  "envoy/annotations/deprecation.proto" \
  "envoy/annotations/resource.proto" \
  "envoy/config/accesslog/v3/accesslog.proto" \
  "envoy/config/bootstrap/v3/bootstrap.proto" \
  "envoy/config/cluster/v3/circuit_breaker.proto" \
  "envoy/config/cluster/v3/cluster.proto" \
  "envoy/config/cluster/v3/filter.proto" \
  "envoy/config/cluster/v3/outlier_detection.proto" \
  "envoy/config/core/v3/address.proto" \
  "envoy/config/core/v3/backoff.proto" \
  "envoy/config/core/v3/base.proto" \
  "envoy/config/core/v3/config_source.proto" \
  "envoy/config/core/v3/event_service_config.proto" \
  "envoy/config/core/v3/extension.proto" \
  "envoy/config/core/v3/grpc_service.proto" \
  "envoy/config/core/v3/health_check.proto" \
  "envoy/config/core/v3/http_uri.proto" \
  "envoy/config/core/v3/protocol.proto" \
  "envoy/config/core/v3/proxy_protocol.proto" \
  "envoy/config/core/v3/socket_option.proto" \
  "envoy/config/core/v3/substitution_format_string.proto" \
  "envoy/config/endpoint/v3/endpoint.proto" \
  "envoy/config/endpoint/v3/endpoint_components.proto" \
  "envoy/config/endpoint/v3/load_report.proto" \
  "envoy/config/listener/v3/api_listener.proto" \
  "envoy/config/listener/v3/listener.proto" \
  "envoy/config/listener/v3/listener_components.proto" \
  "envoy/config/listener/v3/udp_listener_config.proto" \
  "envoy/config/metrics/v3/stats.proto" \
  "envoy/config/overload/v3/overload.proto" \
  "envoy/config/rbac/v3/rbac.proto" \
  "envoy/config/route/v3/route.proto" \
  "envoy/config/route/v3/route_components.proto" \
  "envoy/config/route/v3/scoped_route.proto" \
  "envoy/config/trace/v3/http_tracer.proto" \
  "envoy/extensions/clusters/aggregate/v3/cluster.proto" \
  "envoy/extensions/filters/http/router/v3/router.proto" \
  "envoy/extensions/filters/common/fault/v3/fault.proto" \
  "envoy/extensions/filters/http/fault/v3/fault.proto" \
  "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.proto" \
  "envoy/extensions/transport_sockets/tls/v3/cert.proto" \
  "envoy/extensions/transport_sockets/tls/v3/common.proto" \
  "envoy/extensions/transport_sockets/tls/v3/secret.proto" \
  "envoy/extensions/transport_sockets/tls/v3/tls.proto" \
  "envoy/service/cluster/v3/cds.proto" \
  "envoy/service/discovery/v3/ads.proto" \
  "envoy/service/discovery/v3/discovery.proto" \
  "envoy/service/endpoint/v3/eds.proto" \
  "envoy/service/listener/v3/lds.proto" \
  "envoy/service/load_stats/v3/lrs.proto" \
  "envoy/service/route/v3/rds.proto" \
  "envoy/service/route/v3/srds.proto" \
  "envoy/service/status/v3/csds.proto" \
  "envoy/type/matcher/v3/metadata.proto" \
  "envoy/type/matcher/v3/node.proto" \
  "envoy/type/matcher/v3/number.proto" \
  "envoy/type/matcher/v3/path.proto" \
  "envoy/type/matcher/v3/regex.proto" \
  "envoy/type/matcher/v3/string.proto" \
  "envoy/type/matcher/v3/struct.proto" \
  "envoy/type/matcher/v3/value.proto" \
  "envoy/type/metadata/v3/metadata.proto" \
  "envoy/type/tracing/v3/custom_tag.proto" \
  "envoy/type/v3/http.proto" \
  "envoy/type/v3/percent.proto" \
  "envoy/type/v3/range.proto" \
  "envoy/type/v3/semantic_version.proto" \
  "google/api/annotations.proto" \
  "google/api/expr/v1alpha1/checked.proto" \
  "google/api/expr/v1alpha1/syntax.proto" \
  "google/api/http.proto" \
  "google/protobuf/any.proto" \
  "google/protobuf/descriptor.proto" \
  "google/protobuf/duration.proto" \
  "google/protobuf/empty.proto" \
  "google/protobuf/struct.proto" \
  "google/protobuf/timestamp.proto" \
  "google/protobuf/wrappers.proto" \
  "google/rpc/status.proto" \
  "src/proto/grpc/auth/v1/authz_policy.proto" \
  "src/proto/grpc/gcp/altscontext.proto" \
  "src/proto/grpc/gcp/handshaker.proto" \
  "src/proto/grpc/gcp/transport_security_common.proto" \
  "src/proto/grpc/health/v1/health.proto" \
  "src/proto/grpc/lb/v1/load_balancer.proto" \
  "third_party/istio/security/proto/providers/google/meshca.proto" \
  "udpa/data/orca/v1/orca_load_report.proto" \
  "udpa/annotations/migrate.proto" \
  "udpa/annotations/security.proto" \
  "udpa/annotations/sensitive.proto" \
  "udpa/annotations/status.proto" \
  "udpa/annotations/versioning.proto" \
  "udpa/type/v1/typed_struct.proto" \
  "xds/core/v3/authority.proto" \
  "xds/core/v3/collection_entry.proto" \
  "xds/core/v3/context_params.proto" \
  "xds/core/v3/resource_locator.proto" \
  "xds/core/v3/resource_name.proto" \
  "xds/core/v3/resource.proto" \
  "validate/validate.proto")

INCLUDE_OPTIONS="-I=$PWD/third_party/udpa \
  -I=$PWD/third_party/envoy-api \
  -I=$PWD/third_party/googleapis \
  -I=$PWD/third_party/protobuf/src \
  -I=$PWD/third_party/protoc-gen-validate \
  -I=$PWD"

for i in "${proto_files[@]}"
do
  echo "Compiling: ${i}"
  $PROTOC \
    $INCLUDE_OPTIONS \
    $i \
    --upb_out=$UPB_OUTPUT_DIR \
    --plugin=protoc-gen-upb=$UPB_PLUGIN
  # In PHP build Makefile, the files with .upb.c suffix collide .upbdefs.c suffix due to a PHP buildsystem bug.
  # Work around this by placing the generated files with ".upbdefs.h" and ".upbdefs.c" suffix under a different directory.
  # See https://github.com/grpc/grpc/issues/23307
  $PROTOC \
    $INCLUDE_OPTIONS \
    $i \
    --upb_out=$UPBDEFS_OUTPUT_DIR \
    --plugin=protoc-gen-upb=$UPBDEFS_PLUGIN    
done
