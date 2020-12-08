#!/usr/bin/env bash
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

set -ex -o igncr || set -ex

mkdir -p /var/local/git
git clone /var/local/jenkins/grpc /var/local/git/grpc
(cd /var/local/jenkins/grpc/ && git submodule foreach 'cd /var/local/git/grpc \
&& git submodule update --init --reference /var/local/jenkins/grpc/${name} \
${name}')
cd /var/local/git/grpc

VIRTUAL_ENV=$(mktemp -d)
virtualenv "$VIRTUAL_ENV"
PYTHON="$VIRTUAL_ENV"/bin/python
"$PYTHON" -m pip install --upgrade pip
"$PYTHON" -m pip install --upgrade grpcio-tools google-api-python-client google-auth-httplib2 oauth2client

# Prepare generated Python code.
TOOLS_DIR=tools/run_tests
PROTO_SOURCE_DIR=src/proto/grpc/testing
PROTO_DEST_DIR="$TOOLS_DIR"/"$PROTO_SOURCE_DIR"
mkdir -p "$PROTO_DEST_DIR"
touch "$TOOLS_DIR"/src/__init__.py
touch "$TOOLS_DIR"/src/proto/__init__.py
touch "$TOOLS_DIR"/src/proto/grpc/__init__.py
touch "$TOOLS_DIR"/src/proto/grpc/testing/__init__.py

"$PYTHON" -m grpc_tools.protoc \
    --proto_path=. \
    --python_out="$TOOLS_DIR" \
    --grpc_python_out="$TOOLS_DIR" \
    "$PROTO_SOURCE_DIR"/test.proto \
    "$PROTO_SOURCE_DIR"/messages.proto \
    "$PROTO_SOURCE_DIR"/empty.proto

HEALTH_PROTO_SOURCE_DIR=src/proto/grpc/health/v1
HEALTH_PROTO_DEST_DIR=${TOOLS_DIR}/${HEALTH_PROTO_SOURCE_DIR}
mkdir -p ${HEALTH_PROTO_DEST_DIR}
touch "$TOOLS_DIR"/src/proto/grpc/health/__init__.py
touch "$TOOLS_DIR"/src/proto/grpc/health/v1/__init__.py

"$PYTHON" -m grpc_tools.protoc \
    --proto_path=. \
    --python_out=${TOOLS_DIR} \
    --grpc_python_out=${TOOLS_DIR} \
    ${HEALTH_PROTO_SOURCE_DIR}/health.proto

(cd src/ruby && bundle && rake compile)

GRPC_VERBOSITY=debug GRPC_TRACE=xds_client,xds_resolver,xds_cluster_manager_lb,cds_lb,eds_lb,priority_lb,xds_cluster_impl_lb,weighted_target_lb "$PYTHON" \
  tools/run_tests/run_xds_tests.py \
    --test_case="all,path_matching,header_matching" \
    --project_id=grpc-testing \
    --source_image=projects/grpc-testing/global/images/xds-test-server-2 \
    --path_to_server_binary=/java_server/grpc-java/interop-testing/build/install/grpc-interop-testing/bin/xds-test-server \
    --gcp_suffix=$(date '+%s') \
    --verbose \
    --client_cmd='ruby src/ruby/pb/test/xds_client.rb --server=xds:///{server_uri} --stats_port={stats_port} --qps={qps} {fail_on_failed_rpc} {rpcs_to_send} {metadata_to_send}'
