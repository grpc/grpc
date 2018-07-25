#!/bin/bash
# Copyright 2017 gRPC authors.
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

# Build portability tests with an updated submodule

set -ex

# change to grpc repo root
cd $(dirname $0)/../../..

source tools/internal_ci/helper_scripts/prepare_build_linux_rc

# Update submodule to be tested at HEAD
(cd third_party/$RUN_TESTS_FLAGS && git fetch --all && git checkout origin/master)
tools/buildgen/generate_projects.sh

if [ "$RUN_TESTS_FLAGS" == "protobuf" ]
then
  # Upgrade bazel.
  # make_grpcio_tools.py requires bazel >=0.13.1 to run (Kokoro workers only have bazel 0.9)
  curl -fSsL -O https://github.com/bazelbuild/bazel/releases/download/0.13.1/bazel-0.13.1-installer-linux-x86_64.sh
  chmod +x ./bazel-0.13.1-installer-linux-x86_64.sh
  ./bazel-0.13.1-installer-linux-x86_64.sh --user
  rm -f ./bazel-0.13.1-installer-linux-x86_64.sh
  export PATH="$PATH:$HOME/bin"

  tools/distrib/python/make_grpcio_tools.py
fi

# commit so that changes are passed to Docker
git -c user.name='foo' -c user.email='foo@google.com' commit -a -m 'Update submodule'

tools/run_tests/run_tests_matrix.py -f linux --inner_jobs 4 -j 4 --internal_ci --build_only
