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

# Regenerates gRPC service stubs from proto files.
set -e
cd $(dirname $0)/../..

# protoc and grpc_*_plugin binaries can be obtained by running
# $ bazel build @com_google_protobuf//:protoc //src/compiler:all
PROTOC=bazel-bin/external/com_google_protobuf/protoc
PLUGIN=protoc-gen-grpc=bazel-bin/src/compiler/grpc_csharp_plugin

EXAMPLES_DIR=src/csharp/Grpc.Examples
HEALTHCHECK_DIR=src/csharp/Grpc.HealthCheck
REFLECTION_DIR=src/csharp/Grpc.Reflection
TESTING_DIR=src/csharp/Grpc.IntegrationTesting

$PROTOC --plugin=$PLUGIN --csharp_out=$EXAMPLES_DIR --grpc_out=$EXAMPLES_DIR \
    -I src/proto src/proto/math/math.proto

$PROTOC --plugin=$PLUGIN --csharp_out=$HEALTHCHECK_DIR --grpc_out=$HEALTHCHECK_DIR \
    -I src/proto src/proto/grpc/health/v1/health.proto
    
$PROTOC --plugin=$PLUGIN --csharp_out=$REFLECTION_DIR --grpc_out=$REFLECTION_DIR \
    -I src/proto src/proto/grpc/reflection/v1alpha/reflection.proto

# Put grpc/core/stats.proto in a subdirectory to avoid collision with grpc/testing/stats.proto
mkdir -p $TESTING_DIR/CoreStats
$PROTOC --plugin=$PLUGIN --csharp_out=$TESTING_DIR/CoreStats --grpc_out=$TESTING_DIR/CoreStats \
    -I src/proto src/proto/grpc/core/stats.proto

# TODO(jtattermusch): following .proto files are a bit broken and import paths
# don't match the package names. Setting -I to the correct value src/proto
# breaks the code generation.
$PROTOC --plugin=$PLUGIN --csharp_out=$TESTING_DIR --grpc_out=$TESTING_DIR \
    -I . src/proto/grpc/testing/{control,echo_messages,empty,empty_service,messages,metrics,payloads,benchmark_service,report_qps_scenario_service,worker_service,stats,test}.proto
