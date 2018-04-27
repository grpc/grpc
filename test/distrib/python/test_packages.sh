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

set -ex

cd "$(dirname "$0")"

shopt -s nullglob

if [[ "$1" == "binary" ]]
then
  echo "Testing Python binary distribution"
  ARCHIVES=("$EXTERNAL_GIT_ROOT"/input_artifacts/grpcio-[0-9]*.whl)
  TOOLS_ARCHIVES=("$EXTERNAL_GIT_ROOT"/input_artifacts/grpcio_tools-[0-9]*.whl)
else
  echo "Testing Python source distribution"
  ARCHIVES=("$EXTERNAL_GIT_ROOT"/input_artifacts/grpcio-[0-9]*.tar.gz)
  TOOLS_ARCHIVES=("$EXTERNAL_GIT_ROOT"/input_artifacts/grpcio-tools-[0-9]*.tar.gz)
  HEALTH_ARCHIVES=("$EXTERNAL_GIT_ROOT"/input_artifacts/grpcio-health-checking-[0-9]*.tar.gz)
  REFLECTION_ARCHIVES=("$EXTERNAL_GIT_ROOT"/input_artifacts/grpcio-reflection-[0-9]*.tar.gz)
fi

VIRTUAL_ENV=$(mktemp -d)
virtualenv "$VIRTUAL_ENV"
PYTHON=$VIRTUAL_ENV/bin/python
"$PYTHON" -m pip install --upgrade six pip

function at_least_one_installs() {
  for file in "$@"; do
    if "$PYTHON" -m pip install "$file"; then
      return 0
    fi
  done
  return 1
}


#
# Install our distributions in order of dependencies
#

at_least_one_installs "${ARCHIVES[@]}"
at_least_one_installs "${TOOLS_ARCHIVES[@]}"

if [[ "$1" == "source" ]]
then
  echo "Testing Python health and reflection packages"
  at_least_one_installs "${HEALTH_ARCHIVES[@]}"
  at_least_one_installs "${REFLECTION_ARCHIVES[@]}"
fi


#
# Test our distributions
#

# TODO(jtattermusch): add a .proto file to the distribtest, generate python
# code from it and then use the generated code from distribtest.py
"$PYTHON" -m grpc.tools.protoc --help

"$PYTHON" distribtest.py
