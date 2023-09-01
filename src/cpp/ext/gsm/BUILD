# gRPC Bazel BUILD file.
#
# Copyright 2023 gRPC authors.
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
    name = "gsm_observability",
    srcs = [
        "gsm_observability.cc",
    ],
    hdrs = [
        "gsm_observability.h",
    ],
    external_deps = [
        "absl/container:flat_hash_set",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "otel/sdk/src/metrics",
    ],
    language = "c++",
    visibility = ["//:__subpackages__"],
    deps = [
        "//:gpr_platform",
        "//src/cpp/ext/otel:otel_plugin",
    ],
)
