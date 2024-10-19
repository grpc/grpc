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
    name = "lb",
    visibility = "public",
)

grpc_internal_proto_library(
    name = "load_balancer_proto",
    srcs = ["load_balancer.proto"],
    has_services = True,
    deps = [
        "@com_google_protobuf//:duration_proto",
        "@com_google_protobuf//:timestamp_proto",
    ],
)

grpc_cc_proto_library(
    name = "load_balancer_cc_proto",
    deps = ["load_balancer_proto"],
)

grpc_cc_grpc_library(
    name = "load_balancer_cc_grpc",
    srcs = ["load_balancer_proto"],
    deps = ["load_balancer_cc_proto"],
)

grpc_internal_proto_library(
    name = "load_reporter_proto",
    srcs = ["load_reporter.proto"],
    has_services = True,
    deps = [
        "@com_google_protobuf//:duration_proto",
    ],
)

grpc_cc_proto_library(
    name = "load_reporter_cc_proto",
    deps = ["load_reporter_proto"],
)

grpc_cc_grpc_library(
    name = "load_reporter_cc_grpc",
    srcs = ["load_reporter_proto"],
    deps = ["load_reporter_cc_proto"],
)
