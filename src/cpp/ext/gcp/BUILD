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
    name = "observability",
    srcs = [
        "observability.cc",
    ],
    hdrs = [
        "//:include/grpcpp/ext/gcp_observability.h",
    ],
    external_deps = [
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/time",
        "absl/types:optional",
        "google/api:monitored_resource_cc_proto",
        "googleapis_monitoring_grpc_service",
        "googleapis_trace_grpc_service",
        "opencensus-stats",
        "opencensus-stats-stackdriver_exporter",
        "opencensus-trace",
        "opencensus-trace-stackdriver_exporter",
    ],
    language = "c++",
    visibility = ["//:__subpackages__"],
    deps = [
        "environment_autodetect",
        "observability_config",
        "observability_logging_sink",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//:grpc++_base",
        "//:grpc_opencensus_plugin",
        "//src/core:logging_filter",
        "//src/core:notification",
    ],
)

grpc_cc_library(
    name = "observability_config",
    srcs = [
        "observability_config.cc",
    ],
    hdrs = [
        "observability_config.h",
    ],
    external_deps = [
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/types:optional",
    ],
    language = "c++",
    # visibility = ["//test:__subpackages__"],
    deps = [
        "//:gpr",
        "//:grpc_base",
        "//:grpc_public_hdrs",
        "//src/core:env",
        "//src/core:error",
        "//src/core:json",
        "//src/core:json_args",
        "//src/core:json_object_loader",
        "//src/core:json_reader",
        "//src/core:slice",
        "//src/core:slice_refcount",
        "//src/core:status_helper",
        "//src/core:validation_errors",
    ],
)

grpc_cc_library(
    name = "observability_logging_sink",
    srcs = [
        "observability_logging_sink.cc",
    ],
    hdrs = [
        "observability_logging_sink.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/numeric:int128",
        "absl/strings",
        "absl/strings:str_format",
        "absl/types:optional",
        "google/api:monitored_resource_cc_proto",
        "googleapis_logging_cc_proto",
        "googleapis_logging_grpc_service",
        "protobuf_headers",
    ],
    language = "c++",
    visibility = ["//test:__subpackages__"],
    deps = [
        "environment_autodetect",
        "observability_config",
        "//:event_engine_base_hdrs",
        "//:gpr",
        "//:gpr_platform",
        "//:grpc++",
        "//:grpc_opencensus_plugin",
        "//src/core:default_event_engine",
        "//src/core:env",
        "//src/core:json",
        "//src/core:logging_sink",
        "//src/core:time",
        "//src/core:uuid_v4",
    ],
)

grpc_cc_library(
    name = "environment_autodetect",
    srcs = [
        "environment_autodetect.cc",
    ],
    hdrs = [
        "environment_autodetect.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/container:flat_hash_map",
        "absl/functional:any_invocable",
        "absl/meta:type_traits",
        "absl/status",
        "absl/status:statusor",
        "absl/types:optional",
    ],
    language = "c++",
    visibility = [
        "//src/cpp/ext/gcp:__subpackages__",
        "//test:__subpackages__",
    ],
    deps = [
        "//:exec_ctx",
        "//:gpr",
        "//:gpr_platform",
        "//:grpc++",
        "//:grpc_base",
        "//:grpc_trace",
        "//:orphanable",
        "//src/core:closure",
        "//src/core:default_event_engine",
        "//src/core:env",
        "//src/core:error",
        "//src/core:gcp_metadata_query",
        "//src/core:iomgr_fwd",
        "//src/core:load_file",
        "//src/core:slice",
        "//src/core:status_helper",
        "//src/core:time",
    ],
)
