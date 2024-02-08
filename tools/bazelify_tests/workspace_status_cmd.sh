#!/bin/bash
# Copyright 2023 The gRPC Authors
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

GRPC_GIT_COMMIT="$(git rev-parse HEAD)"
echo "STABLE_GRPC_GIT_COMMIT ${GRPC_GIT_COMMIT}"

# produce a value that has name and commit for every submodule
echo -n 'STABLE_GRPC_SUBMODULE_ARCHIVES'
git submodule --quiet foreach 'git_commit="$(git rev-parse HEAD)"; echo -n " ${name}/${git_commit}"'; echo ""

# set info about whether the git workspace is clean/dirty and checksum of git patch
if [ "$(git status --porcelain)" == "" ]
then
  echo "STABLE_GRPC_GIT_WORKSPACE_DIRTY false"
else
  echo "STABLE_GRPC_GIT_WORKSPACE_DIRTY true"
  echo "STABLE_GRPC_UNCOMMITED_PATCH_CHECKSUM $(git diff HEAD | sha256sum | cut -f1 -d' ')"
fi

# Since only --workspace_status_command is allowed by bazel, also include
# status from the "default" workspace status command.
tools/remote_build/workspace_status_kokoro.sh
