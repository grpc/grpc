#!/bin/bash
# Copyright 2020 gRPC authors.
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

PS4='+ $(date "+[%H:%M:%S %Z]")\011 '
set -ex

# change to grpc repo root
pushd "${KOKORO_ARTIFACTS_DIR}/github/grpc"

# needrestart checks which daemons need to be restarted after library
# upgrades. It's useless to us in non-interactive mode.
sudo DEBIAN_FRONTEND=noninteractive apt-get -qq remove needrestart
sudo DEBIAN_FRONTEND=noninteractive apt-get -qq update
sudo DEBIAN_FRONTEND=noninteractive apt-get -qq -y install --auto-remove python3-venv

VIRTUAL_ENV="$(mktemp -d)"
python3 -m venv "${VIRTUAL_ENV}"

# Activate venv without printing garbage.
{ set +x; } 2>/dev/null
echo "Activating Python venv"
source "${VIRTUAL_ENV}/bin/activate"
set -x

python -VV
pip install --upgrade pip==25.2
# Note that these are only test driver's dependencies. gRPC version
# shouldn't matter, as it's only used for getting the LB stats from the client.
pip install --upgrade \
    grpcio-tools==1.74.0 \
    grpcio==1.74.0 \
    xds-protos==1.74.0 \
    google-api-python-client==2.179.0 \
    google-auth-httplib2==0.2.0 \
    oauth2client==4.1.3
pip list

# Prepare generated Python code.
TOOLS_DIR=tools/run_tests
PROTO_SOURCE_DIR=src/proto/grpc/testing
PROTO_DEST_DIR=${TOOLS_DIR}/${PROTO_SOURCE_DIR}
mkdir -p ${PROTO_DEST_DIR}

python -m grpc_tools.protoc \
    --proto_path=. \
    --python_out=${TOOLS_DIR} \
    --grpc_python_out=${TOOLS_DIR} \
    ${PROTO_SOURCE_DIR}/test.proto \
    ${PROTO_SOURCE_DIR}/messages.proto \
    ${PROTO_SOURCE_DIR}/empty.proto

HEALTH_PROTO_SOURCE_DIR=src/proto/grpc/health/v1
HEALTH_PROTO_DEST_DIR=${TOOLS_DIR}/${HEALTH_PROTO_SOURCE_DIR}
mkdir -p ${HEALTH_PROTO_DEST_DIR}

python -m grpc_tools.protoc \
    --proto_path=. \
    --python_out=${TOOLS_DIR} \
    --grpc_python_out=${TOOLS_DIR} \
    ${HEALTH_PROTO_SOURCE_DIR}/health.proto

popd
