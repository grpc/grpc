# Copyright 2018 gRPC authors.
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
load("//bazel:grpc_build_system.bzl", "grpc_package", "grpc_cc_grpc_library")

licenses(["notice"])

grpc_package(
    name = "channelz",
    visibility = "public",
)

proto_library(
    name = "channelz_proto",
    srcs = ["channelz.proto"],
    deps = [
        "@com_google_protobuf//:any_proto",
        "@com_google_protobuf//:duration_proto",
        "@com_google_protobuf//:timestamp_proto",
        "@com_google_protobuf//:wrappers_proto",
    ],
)

cc_proto_library(
    name = "channelz_cc_proto",
    deps = ["channelz_proto"],
)

grpc_cc_grpc_library(
    name = "channelz_cc_grpc",
    srcs = ["channelz_proto"],
    deps = ["channelz_cc_proto"],
)

filegroup(
    name = "channelz_proto_file",
    srcs = [
        "channelz.proto",
    ],
)
