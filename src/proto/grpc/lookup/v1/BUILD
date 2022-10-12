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

load("@rules_proto//proto:defs.bzl", "proto_library")
load("//bazel:grpc_build_system.bzl", "grpc_package", "grpc_proto_library")

licenses(["notice"])

grpc_package(
    name = "src/proto/grpc/lookup/v1",
    visibility = "public",
)

grpc_proto_library(
    name = "rls_proto",
    srcs = ["rls.proto"],
    well_known_protos = True,
)

grpc_proto_library(
    name = "rls_config_proto",
    srcs = ["rls_config.proto"],
    well_known_protos = True,
)

proto_library(
    name = "rls_proto_descriptor",
    srcs = ["rls.proto"],
)

proto_library(
    name = "rls_config_proto_descriptor",
    srcs = ["rls_config.proto"],
    deps = [
        "@com_google_protobuf//:duration_proto",
    ],
)
