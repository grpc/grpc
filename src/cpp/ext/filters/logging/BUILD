# gRPC Bazel BUILD file.
#
# Copyright 2022 gRPC authors.
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

load(
    "//bazel:grpc_build_system.bzl",
    "grpc_cc_library",
)

licenses(["reciprocal"])

package(
    default_visibility = ["//visibility:public"],
    features = [
        "layering_check",
    ],
)

grpc_cc_library(
    name = "logging_sink",
    hdrs = [
        "logging_sink.h",
    ],
    external_deps = ["absl/strings"],
    language = "c++",
    visibility = [
        "//src/cpp/ext/gcp:__subpackages__",
        "//test:__subpackages__",
    ],
    deps = [
        "//:gpr_platform",
        "//src/core:time",
    ],
)

grpc_cc_library(
    name = "logging_filter",
    srcs = [
        "logging_filter.cc",
    ],
    hdrs = [
        "logging_filter.h",
    ],
    external_deps = [
        "absl/status:statusor",
        "absl/strings",
    ],
    language = "c++",
    visibility = [
        "//src/cpp/ext/gcp:__subpackages__",
        "//test:__subpackages__",
    ],
    deps = [
        "logging_sink",
        "//:config",
        "//:gpr",
        "//:gpr_platform",
        "//:grpc_base",
        "//:promise",
        "//src/core:seq",
        "//src/core:try_concurrently",
    ],
)
