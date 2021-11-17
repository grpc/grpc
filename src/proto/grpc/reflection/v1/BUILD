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

load("@rules_proto//proto:defs.bzl", "proto_library")
load("//bazel:grpc_build_system.bzl", "grpc_package", "grpc_proto_library")

licenses(["notice"])

grpc_package(
    name = "reflection_v1",
    visibility = "public",
)

grpc_proto_library(
    name = "reflection_proto",
    srcs = ["reflection.proto"],
)

proto_library(
    name = "reflection_proto_descriptor",
    srcs = ["reflection.proto"],
)

filegroup(
    name = "reflection_proto_file",
    srcs = [
        "reflection.proto",
    ],
)
