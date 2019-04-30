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

load("@grpc_python_dependencies//:requirements.bzl", "requirement")
load("@org_pubref_rules_protobuf//python:rules.bzl", "py_proto_library")

py_proto_library(
    name = "prime_proto",
    protos = ["prime.proto",],
    deps = [requirement("protobuf")],
)

py_binary(
    name = "client",
    testonly = 1,
    srcs = ["client.py"],
    deps = [
        "//src/python/grpcio/grpc:grpcio",
        ":prime_proto",
    ],
    default_python_version = "PY3",
)

py_binary(
    name = "server",
    testonly = 1,
    srcs = ["server.py"],
    deps = [
        "//src/python/grpcio/grpc:grpcio",
        ":prime_proto"
    ] + select({
        "//conditions:default": [requirement("futures")],
        "//:python3": [],
    }),
    default_python_version = "PY3",
)

py_test(
    name = "test/_multiprocessing_example_test",
    srcs = ["test/_multiprocessing_example_test.py"],
    data = [
        ":client",
        ":server"
    ],
    size = "small",
)
