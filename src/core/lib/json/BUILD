# Copyright 2021 gRPC authors.
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

load("//bazel:grpc_build_system.bzl", "grpc_cc_library", "grpc_package")

licenses(["notice"])

grpc_package(
    name = "test/core/promise",
    visibility = "grpc",
)

grpc_cc_library(
    name = "json",
    srcs = [
        "json_reader.cc",
        "json_writer.cc",
    ],
    hdrs = [
        "json.h",
    ],
    deps = [
        "//:gpr_platform",
        "//:error",
        "//:exec_ctx",
    ],
    external_deps = [
        "absl/strings",
    ]
)

grpc_cc_library(
    name = "util",
    srcs = ["json_util.cc"],
    hdrs = ["json_util.h"],
    deps = [
        "//:gpr_platform",
        ":json",
    ],
    external_deps = [
        "absl/strings",
    ]
)