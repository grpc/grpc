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

set -ex

cd "$(dirname "$0")"

ls -lR ../packages/Grpc.Tools.__GRPC_NUGET_VERSION__/tools

PROTOC=../packages/Grpc.Tools.__GRPC_NUGET_VERSION__/tools/linux_x64/protoc
PLUGIN=../packages/Grpc.Tools.__GRPC_NUGET_VERSION__/tools/linux_x64/grpc_csharp_plugin

$PROTOC --plugin=protoc-gen-grpc=$PLUGIN --csharp_out=. --grpc_out=. -I . helloworld.proto

ls *.cs

echo 'Code generation works.'
