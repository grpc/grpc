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

licenses(["notice"])  # 3-clause BSD

package(default_visibility = ["//visibility:public"])

load("//bazel:grpc_build_system.bzl", "grpc_proto_library")
load("//bazel:cc_grpc_library.bzl", "cc_grpc_library")
load("//bazel:python_rules.bzl", "py_proto_library", "py_grpc_library")
load("@grpc_python_dependencies//:requirements.bzl", "requirement")

grpc_proto_library(
    name = "auth_sample",
    srcs = ["protos/auth_sample.proto"],
)

grpc_proto_library(
    name = "hellostreamingworld",
    srcs = ["protos/hellostreamingworld.proto"],
)

# The following three rules demonstrate the usage of the cc_grpc_library rule in
# in a mode compatible with the native proto_library and cc_proto_library rules.
proto_library(
    name = "helloworld_proto",
    srcs = ["protos/helloworld.proto"],
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
    srcs = ["protos/route_guide.proto"],
)

grpc_proto_library(
    name = "keyvaluestore",
    srcs = ["protos/keyvaluestore.proto"],
)

proto_library(
    name = "protos/helloworld_proto",
    srcs = ["protos/helloworld.proto"],
)

py_proto_library(
    name = "helloworld_py_pb2",
    deps = [":protos/helloworld_proto"],
)

py_grpc_library(
    name = "helloworld_py_pb2_grpc",
    srcs = [":protos/helloworld_proto"],
    deps = [":helloworld_py_pb2"],
)

cc_binary(
    name = "greeter_client",
    srcs = ["cpp/helloworld/greeter_client.cc"],
    defines = ["BAZEL_BUILD"],
    deps = [
        ":helloworld_cc_grpc",
        "//:grpc++",
    ],
)

cc_binary(
    name = "greeter_async_client",
    srcs = ["cpp/helloworld/greeter_async_client.cc"],
    defines = ["BAZEL_BUILD"],
    deps = [
        ":helloworld_cc_grpc",
        "//:grpc++",
    ],
)

cc_binary(
    name = "greeter_async_client2",
    srcs = ["cpp/helloworld/greeter_async_client2.cc"],
    defines = ["BAZEL_BUILD"],
    deps = [
        ":helloworld_cc_grpc",
        "//:grpc++",
    ],
)

cc_binary(
    name = "greeter_server",
    srcs = ["cpp/helloworld/greeter_server.cc"],
    defines = ["BAZEL_BUILD"],
    deps = [
        ":helloworld_cc_grpc",
        "//:grpc++",
    ],
)

cc_binary(
    name = "greeter_async_server",
    srcs = ["cpp/helloworld/greeter_async_server.cc"],
    defines = ["BAZEL_BUILD"],
    deps = [
        ":helloworld_cc_grpc",
        "//:grpc++",
    ],
)

cc_binary(
    name = "metadata_client",
    srcs = ["cpp/metadata/greeter_client.cc"],
    defines = ["BAZEL_BUILD"],
    deps = [
        ":helloworld_cc_grpc",
        "//:grpc++",
    ],
)

cc_binary(
    name = "metadata_server",
    srcs = ["cpp/metadata/greeter_server.cc"],
    defines = ["BAZEL_BUILD"],
    deps = [
        ":helloworld_cc_grpc",
        "//:grpc++",
    ],
)

cc_binary(
    name = "lb_client",
    srcs = ["cpp/load_balancing/greeter_client.cc"],
    defines = ["BAZEL_BUILD"],
    deps = [
        ":helloworld_cc_grpc",
        "//:grpc++",
    ],
)

cc_binary(
    name = "lb_server",
    srcs = ["cpp/load_balancing/greeter_server.cc"],
    defines = ["BAZEL_BUILD"],
    deps = [
        ":helloworld_cc_grpc",
        "//:grpc++",
    ],
)

cc_binary(
    name = "compression_client",
    srcs = ["cpp/compression/greeter_client.cc"],
    defines = ["BAZEL_BUILD"],
    deps = [
        ":helloworld_cc_grpc",
        "//:grpc++",
    ],
)

cc_binary(
    name = "compression_server",
    srcs = ["cpp/compression/greeter_server.cc"],
    defines = ["BAZEL_BUILD"],
    deps = [
        ":helloworld_cc_grpc",
        "//:grpc++",
    ],
)

cc_binary(
    name = "keyvaluestore_client",
    srcs = [
        "cpp/keyvaluestore/caching_interceptor.h",
        "cpp/keyvaluestore/client.cc",
    ],
    defines = ["BAZEL_BUILD"],
    deps = [
        ":keyvaluestore",
        "//:grpc++",
    ],
)

cc_binary(
    name = "keyvaluestore_server",
    srcs = ["cpp/keyvaluestore/server.cc"],
    defines = ["BAZEL_BUILD"],
    deps = [
        ":keyvaluestore",
        "//:grpc++",
    ],
)

cc_binary(
    name = "route_guide_client",
    srcs = [
        "cpp/route_guide/helper.cc",
        "cpp/route_guide/helper.h",
        "cpp/route_guide/route_guide_client.cc",
    ],
    data = ["cpp/route_guide/route_guide_db.json"],
    defines = ["BAZEL_BUILD"],
    deps = [
        ":route_guide",
        "//:grpc++",
    ],
)

cc_binary(
    name = "route_guide_server",
    srcs = [
        "cpp/route_guide/helper.cc",
        "cpp/route_guide/helper.h",
        "cpp/route_guide/route_guide_server.cc",
    ],
    data = ["cpp/route_guide/route_guide_db.json"],
    defines = ["BAZEL_BUILD"],
    deps = [
        ":route_guide",
        "//:grpc++",
    ],
)

proto_library(
    name = "route_guide_proto",
    srcs = ["protos/route_guide.proto"],
)
