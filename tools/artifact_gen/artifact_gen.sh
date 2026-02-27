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

BAZEL_QUERY=(tools/bazel query --noimplicit_deps --output=xml --enable_bzlmod --noenable_workspace --)
BAZEL_SHOW_REPO=(tools/bazel mod --enable_bzlmod show_repo --)
EXTERNAL_REPOS=(
    @@cel-spec+
    @@googleapis+
    @@xds+
    @@protoc-gen-validate+
    @@opencensus-proto+
    @@envoy_api+
)

# PHASE 0: query bazel for information we'll need
cd $(dirname $0)/../..
"${BAZEL_QUERY[@]}" 'deps(//test/...)' > tools/artifact_gen/test_deps.xml
"${BAZEL_QUERY[@]}" 'deps(//:all)' > tools/artifact_gen/root_all_deps.xml
"${BAZEL_QUERY[@]}" 'deps(//src/compiler/...)' > tools/artifact_gen/compiler_deps.xml
"${BAZEL_QUERY[@]}" 'kind(alias, "//third_party:*")' > tools/artifact_gen/third_party_alias_deps.xml
"${BAZEL_QUERY[@]}" 'deps(kind("^proto_library", @envoy_api//envoy/...))' > tools/artifact_gen/envoy_api_proto_deps.xml
"${BAZEL_QUERY[@]}" 'deps("@com_google_protobuf//upb:generated_code_support")' > tools/artifact_gen/upb_deps.xml
"${BAZEL_SHOW_REPO[@]}" "${EXTERNAL_REPOS[@]}" > tools/artifact_gen/external_http_archive_deps.log || true
python3 tools/buildgen/parse_http_archives.py < tools/artifact_gen/external_http_archive_deps.log > tools/artifact_gen/external_http_archive_deps.json

# PHASE 1: generate artifacts
cd tools/artifact_gen
../../tools/bazel build -c opt --cxxopt='-std=c++17' :artifact_gen 
bazel-bin/artifact_gen \
	--target_query=`pwd`/test_deps.xml,`pwd`/root_all_deps.xml,`pwd`/compiler_deps.xml,`pwd`/third_party_alias_deps.xml,`pwd`/envoy_api_proto_deps.xml,`pwd`/upb_deps.xml \
	--external_http_archive_query=`pwd`/external_http_archive_deps.json \
	--extra_build_yaml=`pwd`/../../build_handwritten.yaml \
	--templates_dir=`pwd`/../../templates \
	--output_dir=`pwd`/../.. \
	--save_json=true
