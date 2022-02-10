#!/bin/bash
# Copyright 2016 The gRPC Authors
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
# This script is invoked by build_and_run_docker.sh inside a docker
# container. You should never need to call this script on your own.

set -e

if [ "${RELATIVE_COPY_PATH}" == "" ]
then
  mkdir -p /var/local/git
  git clone "${EXTERNAL_GIT_ROOT}" /var/local/git/grpc
  # clone gRPC submodules, use data from locally cloned submodules where possible
  # TODO: figure out a way to eliminate this following shellcheck suppressions
  # shellcheck disable=SC2016,SC1004
  (cd "${EXTERNAL_GIT_ROOT}" && git submodule foreach 'git clone ${EXTERNAL_GIT_ROOT}/${name} /var/local/git/grpc/${name}')
  (cd /var/local/git/grpc && git submodule init)
else
  mkdir -p "/var/local/git/grpc/${RELATIVE_COPY_PATH}"
  cp -r "${EXTERNAL_GIT_ROOT}/${RELATIVE_COPY_PATH}"/* "/var/local/git/grpc/${RELATIVE_COPY_PATH}"
fi

cd /var/local/git/grpc

# ensure the "reports" directory exists
mkdir -p reports

exit_code=0
${DOCKER_RUN_SCRIPT_COMMAND} || exit_code=$?

# copy reports/ dir and files matching one of the patterns to the well-known
# location of report dir mounted to the docker container.
# --parents preserves the directory structure for files matched by find.
cp -r reports/ /var/local/report_dir
find . -name report.xml -exec cp --parents {} /var/local/report_dir \;
find . -name sponge_log.xml -exec cp --parents {} /var/local/report_dir \;
find . -name 'report_*.xml' -exec cp --parents {} /var/local/report_dir \;
chmod -R ugo+r /var/local/report_dir || true

# Move contents of OUTPUT_DIR from under the workspace to a directory that will be visible to the docker host.
if [ "${OUTPUT_DIR}" != "" ]
then
  # create the directory if it doesn't exist yet.
  mkdir -p "${OUTPUT_DIR}"
  mv "${OUTPUT_DIR}" /var/local/output_dir || exit_code=$?
  chmod -R ugo+r /var/local/output_dir || true
fi

if [ -x "$(command -v ccache)" ]
then
  ccache --show-stats || true
fi

exit $exit_code
