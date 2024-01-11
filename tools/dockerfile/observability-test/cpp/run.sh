#!/bin/bash
# Copyright 2022 gRPC authors.
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
cd "$(dirname "$0")"/../..

if [ "$1" = "server" ] ; then
  /grpc/bazel-bin/test/cpp/interop/observability_interop_server \
    --enable_observability=true "${@:2}"

elif [ "$1" = "client" ] ; then
  /grpc/bazel-bin/test/cpp/interop/observability_interop_client \
    --enable_observability=true "${@:2}"

else
  echo "Invalid action: $1. Usage:"
  echo "  $ .../run.sh [server|client] --server_host=<hostname> --server_port=<port> ..."
  exit 1
fi
