#!/bin/bash
# Copyright 2015 gRPC authors.
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

if [ "$CLANG_TIDY_SKIP_DOCKER" == "" ]
then
  # build clang-tidy docker image
  docker build -t grpc_clang_tidy tools/dockerfile/grpc_clang_tidy

  # run clang-tidy against the checked out codebase
  # when modifying the checked-out files, the current user will be impersonated
  # so that the updated files don't end up being owned by "root".
  docker run -e TEST="$TEST" -e CHANGED_FILES="$CHANGED_FILES" -e CLANG_TIDY_ROOT="/local-code" --rm=true -v "${REPO_ROOT}":/local-code --user "$(id -u):$(id -g)" -t grpc_clang_tidy /clang_tidy_all_the_things.sh "$@"
else
  CLANG_TIDY_ROOT="${REPO_ROOT}" tools/dockerfile/grpc_clang_tidy/clang_tidy_all_the_things.sh "$@"
fi
