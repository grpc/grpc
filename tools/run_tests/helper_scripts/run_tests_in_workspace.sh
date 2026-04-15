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
# All cmdline args will be passed to run_tests.py script (executed in the 
# newly created workspace)
set -ex

cd "$(dirname "$0")/../../.."
repo_root="$(pwd)"
export repo_root

echo "Checking if Redis is reachable..."
if python3 -c "import socket; s = socket.socket(socket.AF_INET, socket.SOCK_STREAM); s.settimeout(2); exit(0) if s.connect_ex(('10.76.145.84', 6379)) == 0 else exit(1)"; then
  echo "Redis is reachable on 10.76.145.84:6379"
else
  echo "Warning: Redis is not reachable on 10.76.145.84:6379"
fi

rm -rf "${WORKSPACE_NAME}"
git clone . "${WORKSPACE_NAME}"
# clone gRPC submodules, use data from locally cloned submodules where possible
# shellcheck disable=SC2016,SC1004
git submodule foreach 'cd "${repo_root}/${WORKSPACE_NAME}" \
    && git submodule update --init --reference ${repo_root}/${name} ${name}'

echo "Running run_tests.py in workspace ${WORKSPACE_NAME}"
python3 "${WORKSPACE_NAME}/tools/run_tests/run_tests.py" "$@"
if [ -x "$(command -v ccache)" ]
then
  ccache --show-stats || true
fi
