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

# REQUIRES: Bazel
set -ex
rm -rf src/core/ext/upb-generated
mkdir src/core/ext/upb-generated
cd third_party
cd upb
bazel build :protoc-gen-upb

cd ../..

proto_files=( \
  "google/api/annotations.proto" \
  "google/api/http.proto" \
  "google/protobuf/any.proto" \
  "google/protobuf/descriptor.proto" \
  "google/protobuf/duration.proto" \
  "google/protobuf/empty.proto" \
  "google/protobuf/struct.proto" \
  "google/protobuf/timestamp.proto" \
  "google/protobuf/wrappers.proto" \
  "google/rpc/status.proto" \
  "gogoproto/gogo.proto" \
  "validate/validate.proto" \
  "envoy/type/percent.proto" \
  "envoy/type/range.proto" \
  "envoy/api/v2/core/address.proto" \
  "envoy/api/v2/core/base.proto" \
  "envoy/api/v2/core/config_source.proto" \
  "envoy/api/v2/core/grpc_service.proto" \
  "envoy/api/v2/core/health_check.proto" \
  "envoy/api/v2/core/protocol.proto" \
  "envoy/api/v2/auth/cert.proto" \
  "envoy/api/v2/cluster/circuit_breaker.proto" \
  "envoy/api/v2/cluster/outlier_detection.proto" \
  "envoy/api/v2/discovery.proto" \
  "envoy/api/v2/cds.proto" \
  "envoy/api/v2/eds.proto" \
  "envoy/api/v2/endpoint/endpoint.proto" \
  "envoy/api/v2/endpoint/load_report.proto" \
  "envoy/service/discovery/v2/ads.proto" \
  "envoy/service/load_stats/v2/lrs.proto")

for i in "${proto_files[@]}"
do
  protoc -I=$PWD/third_party/data-plane-api -I=$PWD/third_party/googleapis -I=$PWD/third_party/protobuf -I=$PWD/third_party/protoc-gen-validate $i --upb_out=./src/core/ext/upb-generated --plugin=protoc-gen-upb=third_party/upb/bazel-bin/protoc-gen-upb
done
