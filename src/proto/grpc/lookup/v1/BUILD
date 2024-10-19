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

load("//bazel:grpc_build_system.bzl", "grpc_cc_grpc_library", "grpc_cc_proto_library", "grpc_internal_proto_library", "grpc_package")

licenses(["notice"])

grpc_package(
    name = "src/proto/grpc/lookup/v1",
    visibility = "public",
)

grpc_internal_proto_library(
    name = "rls_proto",
    srcs = ["rls.proto"],
    has_services = True,
)

grpc_cc_proto_library(
    name = "rls_cc_proto",
    deps = ["rls_proto"],
)

grpc_cc_grpc_library(
    name = "rls_cc_grpc",
    srcs = ["rls_proto"],
    deps = ["rls_cc_proto"],
)

grpc_internal_proto_library(
    name = "rls_config_proto",
    srcs = ["rls_config.proto"],
    deps = [
        "@com_google_protobuf//:duration_proto",
    ],
)

grpc_cc_proto_library(
    name = "rls_config_cc_proto",
    deps = ["rls_config_proto"],
)

grpc_cc_grpc_library(
    name = "rls_config_cc_grpc",
    srcs = ["rls_config_proto"],
    deps = ["rls_config_cc_proto"],
)
