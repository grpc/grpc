# Copyright 2017 gRPC authors.
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
    name = "test/core/service_config",
    features = [
        "layering_check",
        "parse_headers",
    ],
)

licenses(["notice"])

grpc_cc_test(
    name = "service_config_test",
    srcs = ["service_config_test.cc"],
    external_deps = [
        "gtest",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
    ],
    deps = [
        "//:config",
        "//:gpr",
        "//:grpc",
        "//:grpc_service_config_impl",
        "//:ref_counted_ptr",
        "//src/core:channel_args",
        "//src/core:grpc_service_config",
        "//src/core:json",
        "//src/core:json_args",
        "//src/core:json_object_loader",
        "//src/core:service_config_parser",
        "//src/core:validation_errors",
        "//test/core/test_util:grpc_test_util",
    ],
)
