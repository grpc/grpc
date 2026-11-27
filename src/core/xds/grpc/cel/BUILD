# Copyright 2026 gRPC authors.
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

# xDS CEL support (gRFC A106).
#
# This is an optional plugin that is not linked into //:grpc, so that the
# core library does not depend on cel-c.  To enable it, depend on
# :xds_cel_matcher and call grpc_core::RegisterXdsCelMatcher() before
# creating any xDS channels or servers.

load(
    "//bazel:grpc_build_system.bzl",
    "grpc_cc_library",
    "grpc_upb_proto_library",
)

licenses(["reciprocal"])

package(
    default_visibility = [
        "//:__subpackages__",
        "//bazel:friends",
    ],
    features = [
        "layering_check",
        "parse_headers",
    ],
)

grpc_upb_proto_library(
    name = "cel_expr_checked_upb",
    deps = ["@dev_cel//proto/cel/expr:checked_proto"],
)

grpc_upb_proto_library(
    name = "cel_expr_syntax_upb",
    deps = ["@dev_cel//proto/cel/expr:syntax_proto"],
)

grpc_cc_library(
    name = "xds_cel",
    srcs = [
        "xds_cel.cc",
    ],
    hdrs = [
        "xds_cel.h",
    ],
    external_deps = [
        "absl/functional:function_ref",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "cel_c_alloc",
        "cel_c_ast",
        "cel_c_ast_proto",
        "cel_c_runtime",
        "cel_c_status",
        "cel_c_status_absl",
        "cel_c_string_view_absl",
        "cel_c_value",
        "re2",
    ],
    tags = ["nofixdeps"],
    deps = [
        ":cel_expr_checked_upb",
        ":cel_expr_syntax_upb",
        "//src/core:grpc_check",
        "//src/core:no_destruct",
        "//src/core:protobuf_any_upbdefs",
        "//src/core:protobuf_duration_upbdefs",
        "//src/core:protobuf_struct_upbdefs",
        "//src/core:protobuf_timestamp_upbdefs",
        "//src/core:protobuf_wrappers_upbdefs",
        "//src/core:upb_utils",
        "@com_google_protobuf//upb/base",
        "@com_google_protobuf//upb/mem",
        "@com_google_protobuf//upb/message",
        "@com_google_protobuf//upb/message:copy",
        "@com_google_protobuf//upb/reflection",
        "@com_google_protobuf//upb/wire",
    ],
)

grpc_cc_library(
    name = "xds_cel_parser",
    srcs = [
        "xds_cel_parser.cc",
    ],
    hdrs = [
        "xds_cel_parser.h",
    ],
    tags = ["nofixdeps"],
    deps = [
        ":cel_expr_checked_upb",
        ":xds_cel",
        "//src/core:protobuf_wrappers_upb",
        "//src/core:upb_utils",
        "//src/core:validation_errors",
        "//src/core:xds_type_upb",
    ],
)

grpc_cc_library(
    name = "xds_cel_request_resolver",
    srcs = [
        "xds_cel_request_resolver.cc",
    ],
    hdrs = [
        "xds_cel_request_resolver.h",
    ],
    external_deps = [
        "absl/functional:function_ref",
        "absl/strings",
        "cel_c_arena",
        "cel_c_error",
        "cel_c_error_code",
        "cel_c_runtime",
        "cel_c_status",
        "cel_c_string_view_absl",
        "cel_c_value",
    ],
    tags = ["nofixdeps"],
    deps = [
        "//src/core:xds_matcher_context",
    ],
)

grpc_cc_library(
    name = "xds_cel_matcher",
    srcs = [
        "xds_cel_matcher.cc",
    ],
    hdrs = [
        "xds_cel_matcher.h",
    ],
    external_deps = [
        "absl/strings",
    ],
    tags = ["nofixdeps"],
    visibility = ["//visibility:public"],
    deps = [
        ":xds_cel",
        ":xds_cel_parser",
        ":xds_cel_request_resolver",
        "//:xds_client",
        "//src/core:down_cast",
        "//src/core:grpc_check",
        "//src/core:unique_type_name",
        "//src/core:validation_errors",
        "//src/core:xds_bootstrap_grpc_builder",
        "//src/core:xds_common_types",
        "//src/core:xds_custom_match_registry",
        "//src/core:xds_matcher",
        "//src/core:xds_matcher_context",
        "//src/core:xds_type_matcher_upb",
    ],
)
