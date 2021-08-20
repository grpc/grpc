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

# TODO(mingcl): See if it is possible to add flags that warns about undefined symbol at link time
grpc_cc_library(
    name = "grpc_transport_binder_client",
    srcs = [
        "channel_create.cc",
        "channel_create.h",
        "jni_utils.cc",
    ],
    hdrs = [
        "channel_create.h",
        "jni_utils.h",
    ],
    external_deps = [
        "absl/strings",
        "absl/time",
    ],
    deps = [
        ":grpc_transport_binder_client_impl",
        "//:gpr",
        "//:gpr_base",
        "//:grpc",
        "//:grpc++_base",
        "//:grpc_base",
        "//:grpc_base_c",
        "//:grpc_codegen",
        "//src/core/ext/transport/binder/transport",
        "//src/core/ext/transport/binder/utils:transport_stream_receiver",
        "//src/core/ext/transport/binder/wire_format:binder_android",
        "//src/core/ext/transport/binder/wire_format:wire_reader",
    ],
)

grpc_cc_library(
    name = "grpc_transport_binder_client_impl",
    srcs = ["channel_create_impl.cc"],
    hdrs = ["channel_create_impl.h"],
    external_deps = [],
    deps = [
        "//:gpr",
        "//:gpr_base",
        "//:grpc",
        "//:grpc++_base",
        "//:grpc_base",
        "//:grpc_base_c",
        "//:grpc_codegen",
        "//src/core/ext/transport/binder/transport",
        "//src/core/ext/transport/binder/wire_format:binder",
    ],
)
