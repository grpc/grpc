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
    default_visibility = ["//visibility:private"],
    features = [
        "layering_check",
        "-parse_headers",
    ],
)

# This is an EXPERIMENTAL target subject to change.
grpc_cc_library(
    name = "observability",
    srcs = [
        "observability.cc",
    ],
    hdrs = [
        "observability.h",
    ],
    external_deps = [
        "opencensus-trace",
        "opencensus-trace-stackdriver_exporter",
        "opencensus-stats-stackdriver_exporter",
    ],
    language = "c++",
    tags = ["nofixdeps"],
    visibility = ["//test:__subpackages__"],
    deps = [
        "//:gpr",
        "//:grpc_opencensus_plugin",
    ],
)
