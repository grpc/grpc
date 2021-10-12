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
    features = [
        "layering_check",
    ],
)

grpc_cc_library(
    name = "transaction",
    srcs = [
        "transaction.cc",
    ],
    hdrs = [
        "transaction.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/strings",
    ],
    deps = [
        "//:gpr",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "binder",
    srcs = [
        "binder_constants.cc",
    ],
    hdrs = [
        "binder.h",
        "binder_constants.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/status",
        "absl/strings",
    ],
    deps = [
        "//:gpr",
        "//:gpr_platform",
        "//:orphanable",
    ],
)

grpc_cc_library(
    name = "binder_android",
    srcs = [
        "binder_android.cc",
    ],
    hdrs = [
        "binder_android.h",
    ],
    external_deps = [
        "absl/synchronization",
        "absl/memory",
    ],
    deps = [
        ":binder",
        ":wire_reader",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "wire_writer",
    srcs = ["wire_writer.cc"],
    hdrs = ["wire_writer.h"],
    external_deps = [
        "absl/container:flat_hash_map",
        "absl/strings",
    ],
    deps = [
        ":binder",
        ":transaction",
        "//:gpr",
        "//:gpr_base",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "wire_reader",
    srcs = ["wire_reader_impl.cc"],
    hdrs = [
        "wire_reader.h",
        "wire_reader_impl.h",
    ],
    external_deps = [
        "absl/container:flat_hash_map",
        "absl/synchronization",
        "absl/memory",
        "absl/status:statusor",
    ],
    deps = [
        ":binder",
        ":wire_writer",
        "//:gpr",
        "//:gpr_platform",
        "//:orphanable",
        "//src/core/ext/transport/binder/security_policy:security_policy_interface",
        "//src/core/ext/transport/binder/utils:transport_stream_receiver",
    ],
)
