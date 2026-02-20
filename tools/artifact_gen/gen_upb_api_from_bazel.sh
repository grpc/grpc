#!/bin/bash
# Copyright 2025 The gRPC Authors
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

export CC=`which gcc`
export CXX=`which g++`
GCC_VERSION=$(g++ --version | grep -Eo '[0-9]{1,2}\.[0-9]{1,2}\.[0-9]{1,2}' | head -n 1)

if [[ ${GCC_VERSION} > "15.0.0" ]]; then
  BAZEL_BUILD_ARTIFACT=(../../tools/bazel --bazelrc ../../tools/artifact_gen/fix_absl_g++15_linker_error_workaround.bazelrc build)
  BAZEL_BUILD=(tools/bazel --bazelrc tools/artifact_gen/fix_absl_g++15_linker_error_workaround.bazelrc build)
else
  BAZEL_BUILD_ARTIFACT=(../../tools/bazel build)
  BAZEL_BUILD=(tools/bazel build)
fi


# cd to repo root
cd "$(dirname "$0")/../.."

# Build the C++ generator. This must be done from within the sub-workspace.
(cd tools/artifact_gen && "${BAZEL_BUILD_ARTIFACT[@]}" //:gen_upb_api_from_bazel)

# Now that the tool is built, we can move the executable to a temporary location
# to avoid issues with the nested bazel workspaces.
TMP_DIR=$(mktemp -d)
if [ -f tools/artifact_gen/bazel-bin/gen_upb_api_from_bazel.exe ]; then
  cp tools/artifact_gen/bazel-bin/gen_upb_api_from_bazel.exe "${TMP_DIR}/"
  EXECUTABLE="${TMP_DIR}/gen_upb_api_from_bazel.exe"
else
  cp tools/artifact_gen/bazel-bin/gen_upb_api_from_bazel "${TMP_DIR}/"
  EXECUTABLE="${TMP_DIR}/gen_upb_api_from_bazel"
fi
# Clean bazel generated files from sub-workspace to avoid conflicts
rm -rf tools/artifact_gen/bazel-*

# Clean existing generated files
${EXECUTABLE} --mode=clean $@

UPB_RULES_XML=$(mktemp)
DEPS_XML=$(mktemp)
trap "rm -f ${UPB_RULES_XML} ${DEPS_XML}; rm -rf ${TMP_DIR}" EXIT

# Query for upb rules from the main grpc workspace. This must be run from the root.
tools/bazel query --output xml --noimplicit_deps //:all > "${UPB_RULES_XML}"

# Now we can use the generator to get the list of deps.
DEPS_LIST=$(${EXECUTABLE} \
              --mode=list_deps \
              --upb_rules_xml="${UPB_RULES_XML}")

# Query for all the dependencies of the upb rules. This must be run from the root.
if [[ -n "${DEPS_LIST}" ]]; then
  DEPS_QUERY="deps($(echo "${DEPS_LIST}" | sed 's/ / + /g'))"
  tools/bazel query --output xml --noimplicit_deps "${DEPS_QUERY}" > "${DEPS_XML}"
else
  # If there are no dependencies, create an empty XML file
  echo '<?xml version="1.0" encoding="UTF-8" standalone="no"?><query/>' > "${DEPS_XML}"
fi

# Get the list of upb targets to build
BUILD_TARGETS=$(${EXECUTABLE} \
                  --mode=list_build_targets \
                  --upb_rules_xml="${UPB_RULES_XML}")

# Build the upb targets from the root.
if [[ -n "${BUILD_TARGETS}" ]]; then
  "${BAZEL_BUILD[@]}" ${BUILD_TARGETS}
fi

# Run the C++ program to copy the generated files.
${EXECUTABLE} \
  --mode=generate_and_copy \
  --upb_rules_xml="${UPB_RULES_XML}" \
  --deps_xml="${DEPS_XML}" \
  "$@"
