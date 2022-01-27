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
# This script is invoked by build_docker_and_run_tests.sh inside a docker
# container. You should never need to call this script on your own.

set -e

export CONFIG=${config:-opt}
export ASAN_SYMBOLIZER_PATH=/usr/bin/llvm-symbolizer
export PATH=$PATH:/usr/bin/llvm-symbolizer

mkdir -p /var/local/git
git clone /var/local/jenkins/grpc /var/local/git/grpc
# clone gRPC submodules, use data from locally cloned submodules where possible
# TODO: figure out a way to eliminate this shellcheck suppression:
# shellcheck disable=SC2016
(cd /var/local/jenkins/grpc/ && git submodule foreach 'git clone /var/local/jenkins/grpc/${name} /var/local/git/grpc/${name}')
(cd /var/local/git/grpc/ && git submodule init)

mkdir -p reports

$POST_GIT_STEP

exit_code=0

$RUN_TESTS_COMMAND || exit_code=$?

# The easiest way to copy all the reports files from inside of
# the docker container is to zip them and then copy the zip.
zip -r reports.zip reports
find . -name report.xml -print0 | xargs -0 -r zip reports.zip
find . -name sponge_log.xml -print0 | xargs -0 -r zip reports.zip
find . -name 'report_*.xml' -print0 | xargs -0 -r zip reports.zip

exit $exit_code
