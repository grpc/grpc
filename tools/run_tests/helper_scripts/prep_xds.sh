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

set -ex

# change to grpc repo root
cd "$(dirname "$0")/../../.."

sudo apt-get install -y python3-pip
sudo python3 -m pip install --upgrade pip
sudo python3 -m pip install --upgrade setuptools
sudo python3 -m pip install --upgrade grpcio==1.31.0 grpcio-tools==1.31.0 protobuf google-api-python-client google-auth-httplib2 oauth2client xds-protos

# Prepare generated Python code.
TOOLS_DIR=tools/run_tests
PROTO_SOURCE_DIR=src/proto/grpc/testing
PROTO_DEST_DIR=${TOOLS_DIR}/${PROTO_SOURCE_DIR}
mkdir -p ${PROTO_DEST_DIR}

python3 -m grpc_tools.protoc \
    --proto_path=. \
    --python_out=${TOOLS_DIR} \
    --grpc_python_out=${TOOLS_DIR} \
    ${PROTO_SOURCE_DIR}/test.proto \
    ${PROTO_SOURCE_DIR}/messages.proto \
    ${PROTO_SOURCE_DIR}/empty.proto

HEALTH_PROTO_SOURCE_DIR=src/proto/grpc/health/v1
HEALTH_PROTO_DEST_DIR=${TOOLS_DIR}/${HEALTH_PROTO_SOURCE_DIR}
mkdir -p ${HEALTH_PROTO_DEST_DIR}

python3 -m grpc_tools.protoc \
    --proto_path=. \
    --python_out=${TOOLS_DIR} \
    --grpc_python_out=${TOOLS_DIR} \
    ${HEALTH_PROTO_SOURCE_DIR}/health.proto
