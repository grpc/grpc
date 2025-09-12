#!/bin/bash
# Copyright 2025 gRPC authors.
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


set -e

# PHASE 0: query bazel for information we'll need
cd $(dirname $0)/../..

# This json is hardcoded here so it is easier to keep it in sync with Bazel
# queries below
echo """
{
  \"envoy_api\": {
    \"destination\": \"third_party/envoy-api\",
    \"proto_prefix\": \"third_party/envoy-api/\"
  },
  \"com_google_googleapis\": {
    \"destination\": \"third_party/googleapis\",
    \"proto_prefix\": \"third_party/googleapis/\"
  },
  \"com_github_cncf_xds\": {
    \"destination\": \"third_party/xds\",
    \"proto_prefix\": \"third_party/xds/\"
  },
  \"com_envoyproxy_protoc_gen_validate\": {
    \"destination\": \"third_party/protoc-gen-validate\",
    \"proto_prefix\": \"third_party/protoc-gen-validate/\"
  },
  \"opencensus_proto\": {
    \"destination\": \"third_party/opencensus-proto/src\",
    \"proto_prefix\": \"third_party/opencensus-proto/src/\"
  }
}
""" >tools/artifact_gen/external_proto_libraries.json

tools/bazel query --noimplicit_deps --output=xml 'deps(//test/...)' > tools/artifact_gen/test_deps.xml
tools/bazel query --noimplicit_deps --output=xml 'deps(//:all)' > tools/artifact_gen/root_all_deps.xml
tools/bazel query --noimplicit_deps --output=xml 'deps(//src/compiler/...)' > tools/artifact_gen/compiler_deps.xml
tools/bazel query --noimplicit_deps --output=xml 'kind(alias, "//third_party:*")' > tools/artifact_gen/third_party_alias_deps.xml
tools/bazel query --noimplicit_deps --output=xml 'deps(kind("^proto_library", @envoy_api//envoy/...))' > tools/artifact_gen/envoy_api_proto_deps.xml
tools/bazel query --noimplicit_deps --output=xml 'deps("@com_google_protobuf//upb:generated_code_support")' > tools/artifact_gen/upb_deps.xml
# tools/bazel query --noimplicit_deps --output=xml 'kind(http_archive, //external:*)' > tools/artifact_gen/external_http_archive_deps.xml
tools/bazel mod show_repo @envoy_api @com_google_googleapis @com_github_cncf_xds @com_envoyproxy_protoc_gen_validate opencensus-proto > tools/artifact_gen/external_http_archives.bzl

# PHASE 1: generate artifacts
cd tools/artifact_gen
../../tools/bazel build -c opt :artifact_gen 
bazel-bin/artifact_gen \
	--target_query=`pwd`/test_deps.xml,`pwd`/root_all_deps.xml,`pwd`/compiler_deps.xml,`pwd`/third_party_alias_deps.xml,`pwd`/envoy_api_proto_deps.xml,`pwd`/upb_deps.xml \
	--external_proto_libraries=`pwd`/external_proto_libraries.json \
	--external_http_archives_mod_query=`pwd`/external_http_archives.bzl \
	--extra_build_yaml=`pwd`/../../build_handwritten.yaml \
	--templates_dir=`pwd`/../../templates \
	--output_dir=`pwd`/../.. \
	--save_json=true
