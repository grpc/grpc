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
    name = "csm_observability",
    srcs = [
        "csm_observability.cc",
        "metadata_exchange.cc",
    ],
    hdrs = [
        "csm_observability.h",
        "metadata_exchange.h",
    ],
    external_deps = [
        "absl/functional:any_invocable",
        "absl/meta:type_traits",
        "absl/status:statusor",
        "absl/strings",
        "absl/types:optional",
        "absl/types:variant",
        "otel/sdk/src/metrics",
        "otel/sdk:headers",
        "upb_lib",
    ],
    language = "c++",
    visibility = ["//:__subpackages__"],
    deps = [
        "//:gpr_platform",
        "//:grpc_base",
        "//:protobuf_struct_upb",
        "//src/core:channel_args",
        "//src/core:env",
        "//src/core:error",
        "//src/core:json",
        "//src/core:json_args",
        "//src/core:json_object_loader",
        "//src/core:json_reader",
        "//src/core:slice",
        "//src/cpp/ext/otel:otel_plugin",
    ],
)
