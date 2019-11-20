# Copyright 2019 gRPC authors.
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

load("//bazel:grpc_build_system.bzl", "grpc_package", "grpc_proto_library")

licenses(["notice"])  # Apache v2

grpc_package(
    name = "http_over_grpc",
    visibility = "public",
)

grpc_proto_library(
    name = "http_over_grpc_proto",
    srcs = [
        "http_over_grpc.proto",
    ],
    has_services = True,
    well_known_protos = True,
)
