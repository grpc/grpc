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

load("//bazel:grpc_build_system.bzl", "grpc_cc_library", "grpc_cc_test", "grpc_cc_binary", "grpc_package")

licenses(["notice"])  # Apache v2

grpc_package(name = "test/core/security")

load("//test/core/util:grpc_fuzzer.bzl", "grpc_fuzzer")

grpc_fuzzer(
    name = "ssl_server_fuzzer",
    srcs = ["ssl_server_fuzzer.cc"],
    language = "C++",
    corpus = "corpus/ssl_server_corpus",
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/end2end:ssl_test_data",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_library(
    name = "oauth2_utils",
    srcs = ["oauth2_utils.cc"],
    hdrs = ["oauth2_utils.h"],
    language = "C++",
    deps = ["//:grpc"],
    visibility = ["//test/cpp:__subpackages__"],
)

grpc_cc_test(
    name = "auth_context_test",
    srcs = ["auth_context_test.cc"],
    language = "C++",
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:gpr_test_util",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "credentials_test",
    srcs = ["credentials_test.cc"],
    language = "C++",
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:gpr_test_util",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "json_token_test",
    srcs = ["json_token_test.cc"],
    language = "C++",
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:gpr_test_util",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "jwt_verifier_test",
    srcs = ["jwt_verifier_test.cc"],
    language = "C++",
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:gpr_test_util",
        "//test/core/util:grpc_test_util",
    ],
)


grpc_cc_test(
    name = "secure_endpoint_test",
    srcs = ["secure_endpoint_test.cc"],
    language = "C++",
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
    srcs = ["security_connector_test.cc"],
    language = "C++",
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:gpr_test_util",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "ssl_credentials_test",
    srcs = ["ssl_credentials_test.cc"],
    language = "C++",
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:gpr_test_util",
        "//test/core/util:grpc_test_util",
    ]
)

grpc_cc_binary(
    name = "create_jwt",
    srcs = ["create_jwt.cc"],
    language = "C++",
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_binary(
    name = "fetch_oauth2",
    srcs = ["fetch_oauth2.cc"],
    language = "C++",
    deps = [
        ":oauth2_utils",
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_binary(
    name = "verify_jwt",
    srcs = ["verify_jwt.cc"],
    language = "C++",
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "check_gcp_environment_linux_test",
    srcs = ["check_gcp_environment_linux_test.cc"],
    language = "C++",
    deps = [
        "//:alts_util",
        "//:gpr",
        "//:gpr_base",
        "//:grpc",
    ],
)

grpc_cc_test(
    name = "check_gcp_environment_windows_test",
    srcs = ["check_gcp_environment_windows_test.cc"],
    language = "C++",
    deps = [
        "//:alts_util",
        "//:gpr",
        "//:gpr_base",
        "//:grpc",
    ],
)

grpc_cc_test(
    name = "grpc_alts_credentials_options_test",
    srcs = ["grpc_alts_credentials_options_test.cc"],
    language = "C++",
    deps = [
        "//:alts_util",
        "//:gpr",
        "//:grpc",
    ],
)

grpc_cc_test(
    name = "alts_security_connector_test",
    srcs = ["alts_security_connector_test.cc"],
    language = "C++",
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc_base_c", 
        "//:grpc_secure",
        "//:tsi", 
        "//:tsi_interface",
    ],
)
