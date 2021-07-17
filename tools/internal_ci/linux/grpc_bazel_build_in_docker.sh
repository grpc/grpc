#!/usr/bin/env bash
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
#
# Test basic Bazel features
#
# NOTE: No empty lines should appear in this file before igncr is set!
set -ex -o igncr || set -ex

mkdir -p /var/local/git
git clone /var/local/jenkins/grpc /var/local/git/grpc
(cd /var/local/jenkins/grpc/ && git submodule foreach 'cd /var/local/git/grpc \
&& git submodule update --init --reference /var/local/jenkins/grpc/${name} \
${name}')
cd /var/local/git/grpc

# Build all basic targets using the strict warning option which leverages the
# clang compiler to check if sources can pass a set of warning options.
bazel build --define=use_strict_warning=true \
	:all \
	//src/core/... \
	//src/compiler/... \
	//test/... \
	//examples/...

# TODO(jtattersmusch): Adding a build here for --define=grpc_no_xds is not ideal
# and we should find a better place for this. Refer
# https://github.com/grpc/grpc/pull/24536#pullrequestreview-517466531 for more
# details.
# Test that builds with --define=grpc_no_xds=true work.
bazel build //test/cpp/end2end:end2end_test --define=grpc_no_xds=true
# Test that builds that need xDS do not build with --define=grpc_no_xds=true
EXIT_CODE=0
bazel build //test/cpp/end2end:xds_end2end_test --define=grpc_no_xds=true || EXIT_CODE=$?
if [ $EXIT_CODE -eq 0 ]
then
	echo "Building xds_end2end_test succeeded even with --define=grpc_no_xds=true"
	exit 1
fi
