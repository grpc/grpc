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

load("//bazel:grpc_build_system.bzl", "grpc_cc_library", "grpc_cc_test", "grpc_cc_binary")

licenses(["notice"])  # Apache v2

package(
    features = [
        "-layering_check",
        "-parse_headers",
    ],
)

load("//test/core/util:grpc_fuzzer.bzl", "grpc_fuzzer")

grpc_fuzzer(
    name = "ssl_server_fuzzer",
    srcs = ["ssl_server_fuzzer.c"],
    language = "C",
    corpus = "corpus",
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/end2end:ssl_test_data",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_library(
    name = "oauth2_utils",
    srcs = ["oauth2_utils.c"],
    hdrs = ["oauth2_utils.h"],
    language = "C",
    deps = ["//:grpc"],
    visibility = ["//test/cpp:__subpackages__"],
)

grpc_cc_test(
    name = "auth_context_test",
    srcs = ["auth_context_test.c"],
    language = "C",
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:gpr_test_util",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "credentials_test",
    srcs = ["credentials_test.c"],
    language = "C",
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:gpr_test_util",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "secure_endpoint_test",
    srcs = ["secure_endpoint_test.c"],
    language = "C",
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/iomgr:endpoint_tests",
        "//test/core/util:gpr_test_util",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "security_connector_test",
    srcs = ["security_connector_test.c"],
    language = "C",
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:gpr_test_util",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_binary(
    name = "create_jwt",
    srcs = ["create_jwt.c"],
    language = "C",
    deps = [
        "//:gpr",
        "//:grpc",
    ],
)

grpc_cc_binary(
    name = "fetch_oauth2",
    srcs = ["fetch_oauth2.c"],
    language = "C",
    deps = [
        ":oauth2_utils",
        "//:gpr",
        "//:grpc",
    ],
)

grpc_cc_binary(
    name = "verify_jwt",
    srcs = ["verify_jwt.c"],
    language = "C",
    deps = [
        "//:gpr",
        "//:grpc",
    ],
)
