#!/bin/bash
# Copyright 2021 gRPC authors.
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

echo "NOTE: to automagically apply fixes, invoke with --fix"

set -ex

# change to root directory
cd $(dirname $0)/../..
REPO_ROOT=$(pwd)

# grep targets with manual tag, which is not included in a result of bazel build using ...
# let's get a list of them using query command and pass it to gen_compilation_database.py
export MANUAL_TARGETS=$(bazel query 'attr("tags", "manual", tests(//test/cpp/...))' | grep -v _on_ios)

# generate a clang compilation database for all C/C++ sources in the repo.
tools/distrib/gen_compilation_database.py \
  --include_headers \
  --ignore_system_headers \
  --dedup_targets \
  "//:*" \
  "//src/core/..." \
  "//src/cpp/ext/filters/otel/..." \
  "//src/compiler/..." \
  "//test/core/..." \
  "//test/cpp/..." \
  "//fuzztest/..." \
  $MANUAL_TARGETS

# run iwyu against the checked out codebase
# when modifying the checked-out files, the current user will be impersonated
# so that the updated files don't end up being owned by "root".
if [ "$IWYU_SKIP_DOCKER" == "" ]
then
  # build iwyu docker image
  docker build -t grpc_iwyu tools/dockerfile/grpc_iwyu

  docker run \
    -e TEST="$TEST" \
    -e CHANGED_FILES="$CHANGED_FILES" \
    -e IWYU_ROOT="/local-code" \
    --rm=true \
    -v "${REPO_ROOT}":/local-code \
    -v "${HOME/.cache/bazel}":"${HOME/.cache/bazel}" \
    --user "$(id -u):$(id -g)" \
    -t grpc_iwyu /iwyu.sh "$@"
else
  IWYU_ROOT="${REPO_ROOT}" tools/dockerfile/grpc_iwyu/iwyu.sh
fi
