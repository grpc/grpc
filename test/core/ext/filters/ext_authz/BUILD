# Copyright 2024 gRPC authors.
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
    "grpc_cc_test",
    "grpc_package",
)

grpc_package(
    name = "test/core/ext/filters/ext_authz",
    features = [
        "layering_check",
    ],
)

licenses(["notice"])

grpc_cc_test(
    name = "ext_authz_filter_test",
    srcs = ["ext_authz_filter_test.cc"],
    external_deps = [
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "gtest",
    ],
    tags = ["xds_test"],
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc_service_config_impl",
        "//:ref_counted_ptr",
        "//src/core:channel_args",
        "//src/core:down_cast",
        "//src/core:ext_authz_filter",
        "//src/core:grpc_service_config",
        "//src/core:grpc_xds_client",
        "//src/core:slice",
        "//src/core:time",
        "//src/core:xds_common_types",
        "//test/core/event_engine/fuzzing_event_engine",
        "//test/core/filters:filter_matchers",
        "//test/core/filters:filter_test_v2",
        "//test/core/test_util:grpc_test_util",
        "//test/core/xds:xds_transport_fake",
        "@envoy_api//envoy/service/auth/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
    ],
)
