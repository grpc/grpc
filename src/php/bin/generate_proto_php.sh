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

set +e
cd $(dirname $0)/../../..

protoc --proto_path=src/proto/math \
       --php_out=src/php/tests/generated_code \
       --grpc_out=src/php/tests/generated_code \
       --plugin=protoc-gen-grpc=bins/opt/grpc_php_plugin \
       src/proto/math/math.proto

# replace the Empty message with EmptyMessage
# because Empty is a PHP reserved word
output_file=$(mktemp)
sed 's/message Empty/message EmptyMessage/g' \
  src/proto/grpc/testing/empty.proto > $output_file
mv $output_file ./src/proto/grpc/testing/empty.proto
sed 's/grpc\.testing\.Empty/grpc\.testing\.EmptyMessage/g' \
  src/proto/grpc/testing/test.proto > $output_file
mv $output_file ./src/proto/grpc/testing/test.proto

protoc --proto_path=. \
       --php_out=src/php/tests/interop \
       --grpc_out=src/php/tests/interop \
       --plugin=protoc-gen-grpc=bins/opt/grpc_php_plugin \
       src/proto/grpc/testing/messages.proto \
       src/proto/grpc/testing/empty.proto \
       src/proto/grpc/testing/test.proto

# change it back
sed 's/message EmptyMessage/message Empty/g' \
  src/proto/grpc/testing/empty.proto > $output_file
mv $output_file ./src/proto/grpc/testing/empty.proto
sed 's/grpc\.testing\.EmptyMessage/grpc\.testing\.Empty/g' \
  src/proto/grpc/testing/test.proto > $output_file
mv $output_file ./src/proto/grpc/testing/test.proto

