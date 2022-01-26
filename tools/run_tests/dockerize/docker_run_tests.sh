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

# TODO(jtattermusch): added in https://github.com/grpc/grpc/pull/17303, should be removed.
export CONFIG=${config:-opt}

if [ "$RELATIVE_COPY_PATH" == "" ]
then
  mkdir -p /var/local/git
  git clone "$EXTERNAL_GIT_ROOT" /var/local/git/grpc
  # clone gRPC submodules, use data from locally cloned submodules where possible
  # TODO: figure out a way to eliminate this following shellcheck suppressions
  # shellcheck disable=SC2016,SC1004
  (cd "${EXTERNAL_GIT_ROOT}" && git submodule foreach 'git clone ${EXTERNAL_GIT_ROOT}/${name} /var/local/git/grpc/${name}')
  (cd /var/local/git/grpc && git submodule init)
else
  mkdir -p "/var/local/git/grpc/$RELATIVE_COPY_PATH"
  cp -r "$EXTERNAL_GIT_ROOT/$RELATIVE_COPY_PATH"/* "/var/local/git/grpc/$RELATIVE_COPY_PATH"
fi

# ensure the "reports" directory exists
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

# copy reports.zip to the well-known output dir mounted to the
# docker container.
cp reports.zip /var/local/output_dir

if [ -x "$(command -v ccache)" ]
then
  ccache --show-stats || true
fi

exit $exit_code
