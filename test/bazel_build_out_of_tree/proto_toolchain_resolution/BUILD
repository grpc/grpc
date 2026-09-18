# Copyright 2026 The gRPC Authors
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

load("@bazel_skylib//rules:build_test.bzl", "build_test")
load("@grpc//bazel:cc_grpc_library.bzl", "cc_grpc_library")
load("@grpc//bazel:generate_objc.bzl", "generate_objc")
load("@grpc//bazel:python_rules.bzl", "py_grpc_library", "py_proto_library")
load("@protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("@protobuf//bazel:proto_library.bzl", "proto_library")

proto_library(
    name = "helloworld_proto",
    srcs = ["helloworld.proto"],
)

cc_proto_library(
    name = "helloworld_cc_proto",
    deps = [":helloworld_proto"],
)

cc_grpc_library(
    name = "helloworld_cc_grpc",
    srcs = [":helloworld_proto"],
    grpc_only = True,
    deps = [":helloworld_cc_proto"],
)

py_proto_library(
    name = "helloworld_py_pb2",
    use_protobuf = False,
    deps = [":helloworld_proto"],
)

py_grpc_library(
    name = "helloworld_py_pb2_grpc",
    srcs = [":helloworld_proto"],
    deps = [":helloworld_py_pb2"],
)

generate_objc(
    name = "helloworld_objc",
    srcs = ["helloworld.proto"],
    deps = [":helloworld_proto"],
)

build_test(
    name = "proto_toolchain_resolution_build_test",
    targets = [
        ":helloworld_cc_grpc",
        ":helloworld_py_pb2",
        ":helloworld_py_pb2_grpc",
        ":helloworld_objc",
    ],
)
