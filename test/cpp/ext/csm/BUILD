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

load("//bazel:grpc_build_system.bzl", "grpc_cc_test", "grpc_package")

licenses(["notice"])

grpc_package(
    name = "test/cpp/ext/csm",
    visibility = "tests",
    features = [
        "layering_check",
        "parse_headers",
    ],
)

grpc_cc_test(
    name = "csm_observability_test",
    srcs = [
        "csm_observability_test.cc",
    ],
    external_deps = [
        "google_cloud_cpp:opentelemetry",
        "gtest",
        "otel/sdk:headers",
        "otel/sdk/src/metrics",
    ],
    tags = [
        # TODO(b/406886630): Remove after fixing
        "grpc:broken-internally",
        # TODO(b/332369798): Remove after fixing bug
        "grpc:otel-namespace-calamity",
    ],
    deps = [
        "//:grpc++",
        "//src/core:xds_enabled_server",
        "//src/cpp/ext/csm:csm_observability",
        "//src/cpp/ext/otel:otel_plugin",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "metadata_exchange_test",
    srcs = [
        "metadata_exchange_test.cc",
    ],
    external_deps = [
        "absl/functional:any_invocable",
        "google_cloud_cpp:opentelemetry",
        "gtest",
        "otel/api",
        "otel/sdk:headers",
        "otel/sdk/src/metrics",
    ],
    tags = [
        # TODO(b/406886630): Remove after fixing
        "grpc:broken-internally",
        # TODO(b/332369798): Remove after fixing bug
        "grpc:otel-namespace-calamity",
    ],
    deps = [
        "//:call_tracer",
        "//:config",
        "//:gpr",
        "//:grpc++",
        "//src/core:env",
        "//src/cpp/ext/csm:csm_observability",
        "//src/cpp/ext/otel:otel_plugin",
        "//test/core/test_util:grpc_test_util",
        "//test/cpp/end2end:test_service_impl",
        "//test/cpp/ext/otel:otel_test_library",
    ],
)
