# Copyright 2020 the gRPC authors.
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

licenses(["notice"])  # 3-clause BSD

package(default_visibility = ["//visibility:public"])

load("@grpc_python_dependencies//:requirements.bzl", "requirement")
load("@rules_proto//proto:defs.bzl", "proto_library")
load("//bazel:cc_grpc_library.bzl", "cc_grpc_library")
load("//bazel:grpc_build_system.bzl", "grpc_proto_library")
load("//bazel:python_rules.bzl", "py_grpc_library", "py_proto_library")

grpc_proto_library(
    name = "auth_sample",
    srcs = ["auth_sample.proto"],
)

grpc_proto_library(
    name = "hellostreamingworld",
    srcs = ["hellostreamingworld.proto"],
)

# The following three rules demonstrate the usage of the cc_grpc_library rule in
# in a mode compatible with the native proto_library and cc_proto_library rules.
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

grpc_proto_library(
    name = "route_guide",
    srcs = ["route_guide.proto"],
)

proto_library(
    name = "keyvaluestore_proto",
    srcs = ["keyvaluestore.proto"],
)

grpc_proto_library(
    name = "keyvaluestore",
    srcs = ["keyvaluestore.proto"],
)

py_proto_library(
    name = "helloworld_py_pb2",
    deps = [":helloworld_proto"],
)

py_grpc_library(
    name = "helloworld_py_pb2_grpc",
    srcs = [":helloworld_proto"],
    deps = [":helloworld_py_pb2"],
)

proto_library(
    name = "route_guide_proto",
    srcs = [":route_guide.proto"],
)
