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

load("//bazel:grpc_build_system.bzl", "grpc_cc_test")

licenses(["notice"])


grpc_cc_test(
    name = "ext_authz_service_config_parser_test",
    srcs = ["ext_authz_service_config_parser_test.cc"],
    external_deps = [
        "gtest",
    ],
    deps = [
        "//src/core:ext_authz_filter",
        "//test/core/test_util:grpc_test_util",
        "//:grpc",
        "//src/core:json",
        "//src/core:validation_errors",
        "//:config",
        "//src/core:channel_args",
        "@envoy_api//envoy/extensions/filters/http/ext_authz/v3:pkg_cc_proto",
    ],
)