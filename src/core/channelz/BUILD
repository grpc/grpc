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
    "grpc_package",
)

licenses(["notice"])

grpc_package(
    name = "src/core/channelz",
    visibility = "grpc",
)

grpc_cc_library(
    name = "channelz",
    srcs = [
        "channel_trace.cc",
        "channelz.cc",
        "channelz_registry.cc",
    ],
    hdrs = [
        "channel_trace.h",
        "channelz.h",
        "channelz_registry.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/cleanup",
        "absl/container:btree",
        "absl/log",
        "absl/log:check",
        "absl/status:statusor",
        "absl/strings",
        "absl/container:flat_hash_set",
        "absl/container:inlined_vector",
        "absl/functional:function_ref",
        "@com_google_protobuf//upb/base",
        "@com_google_protobuf//upb/mem",
    ],
    deps = [
        "property_list",
        "//:channelz_upb",
        "//:config_vars",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc_public_hdrs",
        "//:grpc_trace",
        "//:parse_address",
        "//:protobuf_any_upb",
        "//:ref_counted_ptr",
        "//:sockaddr_utils",
        "//:uri",
        "//src/core:channel_args",
        "//src/core:connectivity_state",
        "//src/core:dual_ref_counted",
        "//src/core:json",
        "//src/core:json_reader",
        "//src/core:json_writer",
        "//src/core:memory_usage",
        "//src/core:notification",
        "//src/core:per_cpu",
        "//src/core:ref_counted",
        "//src/core:resolved_address",
        "//src/core:shared_bit_gen",
        "//src/core:single_set_ptr",
        "//src/core:slice",
        "//src/core:sync",
        "//src/core:time",
        "//src/core:time_precise",
        "//src/core:upb_utils",
        "//src/core:useful",
    ],
)

grpc_cc_library(
    name = "property_list",
    srcs = ["property_list.cc"],
    hdrs = ["property_list.h"],
    external_deps = [
        "absl/strings",
        "absl/status",
        "absl/container:flat_hash_map",
        "@com_google_protobuf//upb/mem",
        "@com_google_protobuf//upb/text",
    ],
    deps = [
        "//:channelz_property_list_upb",
        "//:channelz_upb",
        "//:gpr",
        "//:protobuf_any_upb",
        "//:protobuf_empty_upb",
        "//src/core:json",
        "//src/core:match",
        "//src/core:time",
        "//src/core:upb_utils",
    ],
)

grpc_cc_library(
    name = "ztrace_collector",
    hdrs = ["ztrace_collector.h"],
    defines = select({
        "//:grpc_no_ztrace": ["GRPC_NO_ZTRACE"],
        "//conditions:default": [],
    }),
    external_deps = [
        "absl/container:flat_hash_set",
    ],
    deps = [
        "channelz",
        "//:gpr",
        "//:grpc_trace",
        "//src/core:json_writer",
        "//src/core:memory_usage",
        "//src/core:single_set_ptr",
        "//src/core:sync",
        "//src/core:time",
    ],
)
