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

trap 'date' DEBUG
set -ex -o igncr || set -ex

mkdir -p /var/local/git
git clone -b master --single-branch --depth=1 https://github.com/grpc/grpc.git /var/local/git/grpc
cd /var/local/git/grpc

python3 -m pip install virtualenv
VIRTUAL_ENV=$(mktemp -d)
python3 -m virtualenv "$VIRTUAL_ENV" -p python3
PYTHON="$VIRTUAL_ENV"/bin/python
"$PYTHON" -m pip install --upgrade pip==19.3.1
# TODO(sergiitk): Unpin grpcio-tools when a version of xds-protos
#   compatible with protobuf 4.X is uploaded to PyPi.
"$PYTHON" -m pip install --upgrade grpcio grpcio-tools==1.48.1 google-api-python-client google-auth-httplib2 oauth2client xds-protos

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

bazel build test/cpp/interop:xds_interop_client

# Test cases "path_matching" and "header_matching" are not included in "all",
# because not all interop clients in all languages support these new tests.
#
# TODO: remove "path_matching" and "header_matching" from --test_case after
# they are added into "all".
GRPC_VERBOSITY=debug GRPC_TRACE=xds_client,xds_resolver,xds_cluster_manager_lb,cds_lb,xds_cluster_resolver_lb,priority_lb,xds_cluster_impl_lb,weighted_target_lb "$PYTHON" \
  tools/run_tests/run_xds_tests.py \
    --halt_after_fail \
    --test_case="ping_pong,circuit_breaking" \
    --project_id=grpc-testing \
    --project_num=830293263384 \
    --source_image=projects/grpc-testing/global/images/xds-test-server-5 \
    --path_to_server_binary=/java_server/grpc-java/interop-testing/build/install/grpc-interop-testing/bin/xds-test-server \
    --gcp_suffix=$(date '+%s') \
    --verbose \
    ${XDS_V3_OPT-} \
    --client_cmd='bazel-bin/test/cpp/interop/xds_interop_client --server=xds:///{server_uri} --stats_port={stats_port} --qps={qps} {fail_on_failed_rpc} \
      {rpcs_to_send} \
      {metadata_to_send}'
