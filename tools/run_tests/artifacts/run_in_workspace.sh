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
#
# Create a workspace in a subdirectory to allow running multiple builds in isolation.
# WORKSPACE_NAME env variable needs to contain name of the workspace to create.
# All cmdline args will be executed as a command.
set -ex

cd "$(dirname "$0")/../../.."
export repo_root=$(pwd)

# TODO: fix file to pass shellcheck

rm -rf "${WORKSPACE_NAME}"
git clone . "${WORKSPACE_NAME}"
# clone gRPC submodules, use data from locally cloned submodules where possible
# shellcheck disable=SC1004,SC2016
git submodule foreach 'cd "${repo_root}/${WORKSPACE_NAME}" \
    && git submodule update --init --reference ${repo_root}/${name} ${name}'

echo "Running in workspace ${WORKSPACE_NAME}"
cd "${WORKSPACE_NAME}"
# shellcheck disable=SC2068
$@
