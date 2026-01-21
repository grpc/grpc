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
load("//test/core/test_util:grpc_fuzzer.bzl", "grpc_fuzz_test")

licenses(["notice"])

grpc_package(
    name = "test/core/credentials/transport/ssl",
    features = [
        "layering_check",
        "parse_headers",
    ],
)

grpc_fuzz_test(
    name = "ssl_server_fuzzer",
    srcs = ["ssl_server_fuzzer.cc"],
    data = [
        "//src/core/tsi/test_creds:ca.pem",
        "//src/core/tsi/test_creds:server1.key",
        "//src/core/tsi/test_creds:server1.pem",
    ],
    external_deps = [
        "absl/log:check",
        "fuzztest",
        "fuzztest_main",
        "absl/synchronization",
    ],
    tags = ["no_windows"],
    deps = [
        "//:event_engine_base_hdrs",
        "//:gpr",
        "//:grpc",
        "//:grpc_core_credentials_header",
        "//:grpc_security_base",
        "//src/core:default_event_engine",
        "//src/core:grpc_check",
        "//src/core:notification",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
    ],
)

grpc_cc_test(
    name = "ssl_credentials_test",
    srcs = ["ssl_credentials_test.cc"],
    external_deps = ["gtest"],
    deps = [
        "//:gpr",
        "//:grpc",
        "//:ref_counted_ptr",
        "//:tsi_ssl_credentials",
        "//src/core:grpc_ssl_credentials",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
    ],
)
