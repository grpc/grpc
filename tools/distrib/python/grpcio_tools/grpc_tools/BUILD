# Copyright 2020 The gRPC authors.
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

package(default_visibility = ["//src/python:__subpackages__"])

load("//bazel:cython_library.bzl", "pyx_library")

NON_BAZEL_ROOT = "../"

cc_library(
    name = "protoc_lib",
    srcs = ["main.cc"],
    hdrs = ["main.h"],
    deps = [
      "@com_google_protobuf//:protoc_lib",
      "//src/compiler:grpc_plugin_support",
    ],
    includes = [NON_BAZEL_ROOT],
)

pyx_library(
    name = "cyprotoc",
    srcs = ["_protoc_compiler.pyx"],
    deps = [":protoc_lib"],
    includes = [NON_BAZEL_ROOT],
)

py_library(
    name = "grpc_tools",
    srcs = ["__init__.py", "protoc.py"],
    deps = [
      ":cyprotoc",
      "@com_google_protobuf//:protobuf_python",
      "//src/python/grpcio/grpc:grpcio",
    ],
    srcs_version = "PY2AND3",
    imports = [NON_BAZEL_ROOT],
)
