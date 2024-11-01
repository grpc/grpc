# Copyright 2017 gRPC authors.
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

load("//bazel:grpc_build_system.bzl", "grpc_cc_grpc_library", "grpc_cc_proto_library", "grpc_internal_proto_library", "grpc_package")

licenses(["notice"])

grpc_package(
    name = "health",
    visibility = "public",
)

grpc_internal_proto_library(
    name = "health_proto",
    srcs = ["health.proto"],
    has_services = True,
)

grpc_cc_proto_library(
    name = "health_cc_proto",
    deps = ["health_proto"],
)

grpc_cc_grpc_library(
    name = "health_cc_grpc",
    srcs = ["health_proto"],
    deps = ["health_cc_proto"],
)

filegroup(
    name = "health_proto_file",
    srcs = [
        "health.proto",
    ],
)
