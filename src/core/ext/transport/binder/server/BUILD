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

load("//bazel:grpc_build_system.bzl", "grpc_cc_library")

licenses(["notice"])

package(
    default_visibility = ["//visibility:public"],
)

grpc_cc_library(
    name = "grpc_transport_binder_server",
    srcs = [
        "binder_server.cc",
        "binder_server_credentials.cc",
    ],
    hdrs = [
        "binder_server.h",
        "binder_server_credentials.h",
    ],
    external_deps = [
        "absl/memory",
        "absl/status",
    ],
    language = "c++",
    deps = [
        "//:grpc++_base",
        "//:grpc_base_c",
        "//src/core/ext/transport/binder/transport",
        "//src/core/ext/transport/binder/wire_format:binder",
        "//src/core/ext/transport/binder/wire_format:binder_android",
    ],
)
