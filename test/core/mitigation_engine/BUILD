# gRPC Bazel BUILD file.
#
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

load("//bazel:grpc_build_system.bzl", "grpc_cc_test", "grpc_package")

grpc_package(
    name = "test/core/mitigation_engine",
    features = [
        "layering_check",
        "parse_headers",
    ],
)

licenses(["notice"])

grpc_cc_test(
    name = "mitigation_engine_provider_test",
    srcs = ["mitigation_engine_provider_test.cc"],
    external_deps = [
        "gtest",
        "absl/strings:string_view",
    ],
    deps = [
        "//:grpc",
        "//:ref_counted_ptr",
        "//src/core:metadata_batch",
        "//src/core:mitigation_engine",
        "//test/core/test_util:grpc_test_util",
    ],
)
