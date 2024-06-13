# Copyright 2023 gRPC authors.
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
    name = "src/proto/grpc/service_config",
    visibility = "public",
)

grpc_proto_library(
    name = "service_config_proto",
    srcs = ["service_config.proto"],
    has_services = False,
    well_known_protos = True,
    deps = [
        "//src/proto/grpc/lookup/v1:rls_config_proto",
        "@com_google_googleapis//google/rpc:code_proto",
    ],
)

proto_library(
    name = "service_config_proto_descriptor",
    srcs = ["service_config.proto"],
    deps = [
        "//src/proto/grpc/lookup/v1:rls_config_proto_descriptor",
        "@com_google_googleapis//google/rpc:code_proto",
        "@com_google_protobuf//:duration_proto",
        "@com_google_protobuf//:struct_proto",
        "@com_google_protobuf//:wrappers_proto",
    ],
)
