#!/bin/sh

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

GRPC_CPP_PLUGIN_PATH=`which grpc_cpp_plugin`

cd `dirname $0`/../..

find ./test -type f -name "*.proto" |
while read p ; do
  echo "processing $p"
  DIR=$(dirname "$p")
  protoc $p --grpc_out=./ --plugin=protoc-gen-grpc=$GRPC_CPP_PLUGIN_PATH
  protoc $p --cpp_out=./
done
