#!/bin/bash

set -ex

# PHASE 0: query bazel for information we'll need
cd $(dirname $0)/../..
tools/bazel query --noimplicit_deps --output=xml 'deps(//test/...)' > tools/artifact_gen/test_deps.xml
tools/bazel query --noimplicit_deps --output=xml 'deps(//:all)' > tools/artifact_gen/root_all_deps.xml
tools/bazel query --noimplicit_deps --output=xml 'deps(//src/compiler/...)' > tools/artifact_gen/compiler_deps.xml
tools/bazel query --noimplicit_deps --output=xml 'kind(alias, "//third_party:*")' > tools/artifact_gen/third_party_alias_deps.xml
tools/bazel query --noimplicit_deps --output=xml 'deps(kind("^proto_library", @envoy_api//envoy/...))' > tools/artifact_gen/envoy_api_proto_deps.xml
tools/bazel query --noimplicit_deps --output=xml 'deps("@com_google_protobuf//upb:generated_code_support__only_for_generated_code_do_not_use__i_give_permission_to_break_me")' > tools/artifact_gen/upb_deps.xml
tools/bazel query --noimplicit_deps --output=xml 'kind(http_archive, //external:*)' > tools/artifact_gen/external_http_archive_deps.xml

# PHASE 1: generate artifacts
cd tools/artifact_gen
bazel build -c opt :artifact_gen 
bazel-bin/artifact_gen \
	--target_query=`pwd`/test_deps.xml,`pwd`/root_all_deps.xml,`pwd`/compiler_deps.xml,`pwd`/third_party_alias_deps.xml,`pwd`/envoy_api_proto_deps.xml,`pwd`/upb_deps.xml \
	--external_http_archive_query=`pwd`/external_http_archive_deps.xml \
	--extra_build_yaml=`pwd`/../../build_handwritten.yaml
