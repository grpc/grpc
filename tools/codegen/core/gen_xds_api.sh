#!/bin/bash

# REQUIRES: Bazel
set -ex

#cd third_party
#git clone https://github.com/haberman/upb.git
#cd upb

#git checkout upbc-cpp
#bazel build :protoc-gen-upb

#cd ../..

proto_files=( \
  "validate/validate.proto" \
  "gogoproto/gogo.proto" \
  "google/rpc/status.proto" \
  "google/api/http.proto" \
  "google/api/annotations.proto" \
  "google/protobuf/any.proto" \
  "google/protobuf/struct.proto" \
  "google/protobuf/wrappers.proto" \
  "google/protobuf/descriptor.proto" \
  "google/protobuf/duration.proto" \
  "google/protobuf/timestamp.proto" \
  "envoy/type/percent.proto" \
  "envoy/api/v2/core/address.proto" \
  "envoy/api/v2/core/health_check.proto" \
  "envoy/api/v2/core/base.proto" \
  "envoy/api/v2/endpoint/endpoint.proto" \
  "envoy/api/v2/discovery.proto" \
  "envoy/api/v2/eds.proto" \
  "envoy/service/discovery/v2/ads.proto" )

for i in "${proto_files[@]}"
do
  protoc -I=$PWD/third_party/data-plane-api -I=$PWD/third_party/googleapis -I=$PWD/third_party/protobuf -I=$PWD/third_party/protoc-gen-validate $i --upb_out=./ --plugin=protoc-gen-upb=third_party/upb/bazel-bin/protoc-gen-upb
done
