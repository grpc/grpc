# gRPC Bazel BUILD file.
#
# Copyright 2019 The gRPC authors.
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
load(
    "@com_github_grpc_grpc//bazel:python_rules.bzl",
    "py2and3_test",
    "py_grpc_library",
    "py_proto_library",
)

package(default_testonly = 1)

proto_library(
    name = "helloworld_proto",
    srcs = ["helloworld.proto"],
    deps = [
        "@com_google_protobuf//:duration_proto",
        "@com_google_protobuf//:timestamp_proto",
    ],
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

py_proto_library(
    name = "duration_py_pb2",
    deps = ["@com_google_protobuf//:duration_proto"],
)

py_proto_library(
    name = "timestamp_py_pb2",
    deps = ["@com_google_protobuf//:timestamp_proto"],
)

py2and3_test(
    name = "import_test",
    srcs = ["helloworld.py"],
    main = "helloworld.py",
    deps = [
        ":duration_py_pb2",
        ":helloworld_py_pb2",
        ":helloworld_py_pb2_grpc",
        ":timestamp_py_pb2",
    ],
)

# Test compatibility of py_proto_library and py_grpc_library rules with
# proto_library targets as deps when the latter use import_prefix and/or
# strip_import_prefix arguments
#
# See namespaced/upper/example for more encompassing tests.
proto_library(
    name = "helloworld_moved_proto",
    srcs = ["helloworld.proto"],
    import_prefix = "google/cloud",
    strip_import_prefix = "",
    deps = [
        "@com_google_protobuf//:duration_proto",
        "@com_google_protobuf//:timestamp_proto",
    ],
)

# Also test the custom plugin execution parameter
py_proto_library(
    name = "helloworld_moved_py_pb2",
    plugin = ":dummy_plugin",
    deps = [":helloworld_moved_proto"],
)

py_grpc_library(
    name = "helloworld_moved_py_pb2_grpc",
    srcs = [":helloworld_moved_proto"],
    deps = [":helloworld_moved_py_pb2"],
)

py2and3_test(
    name = "import_moved_test",
    srcs = ["helloworld_moved.py"],
    main = "helloworld_moved.py",
    deps = [
        ":duration_py_pb2",
        ":helloworld_moved_py_pb2",
        ":helloworld_moved_py_pb2_grpc",
        ":timestamp_py_pb2",
    ],
)

py_binary(
    name = "dummy_plugin",
    srcs = [":dummy_plugin.py"],
    deps = [
        "@com_google_protobuf//:protobuf_python",
    ],
)
