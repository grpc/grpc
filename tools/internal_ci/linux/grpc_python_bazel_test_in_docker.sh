#!/usr/bin/env bash
# Copyright 2018 gRPC authors.
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
# Test full Bazel
#
# NOTE: No empty lines should appear in this file before igncr is set!
set -ex -o igncr || set -ex

mkdir -p /var/local/git
git clone /var/local/jenkins/grpc /var/local/git/grpc
(cd /var/local/jenkins/grpc/ && git submodule foreach 'cd /var/local/git/grpc \
&& git submodule update --init --reference /var/local/jenkins/grpc/${name} \
${name}')
cd /var/local/git/grpc/test
TEST_TARGETS="//src/python/... //tools/distrib/python/grpcio_tools/... //examples/python/..."
BAZEL_FLAGS="--test_output=errors"
bazel test ${BAZEL_FLAGS} ${TEST_TARGETS}
bazel test --config=python_single_threaded_unary_stream ${BAZEL_FLAGS} ${TEST_TARGETS}
bazel test --config=python_poller_engine ${BAZEL_FLAGS} ${TEST_TARGETS}


# TODO(https://github.com/grpc/grpc/issues/19854): Move this to a new Kokoro
# job.
(cd /var/local/git/grpc/bazel/test/python_test_repo;
  bazel test --test_output=errors //...
)
