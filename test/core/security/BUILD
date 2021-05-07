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

load("//bazel:grpc_build_system.bzl", "grpc_cc_binary", "grpc_cc_library", "grpc_cc_test", "grpc_package")

licenses(["notice"])  # Apache v2

grpc_package(name = "test/core/security")

load("//test/core/util:grpc_fuzzer.bzl", "grpc_fuzzer")

grpc_fuzzer(
    name = "alts_credentials_fuzzer",
    srcs = ["alts_credentials_fuzzer.cc"],
    corpus = "corpus/alts_credentials_corpus",
    language = "C++",
    tags = ["no_windows"],
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_fuzzer(
    name = "ssl_server_fuzzer",
    srcs = ["ssl_server_fuzzer.cc"],
    corpus = "corpus/ssl_server_corpus",
    data = [
        "//src/core/tsi/test_creds:ca.pem",
        "//src/core/tsi/test_creds:server1.key",
        "//src/core/tsi/test_creds:server1.pem",
    ],
    language = "C++",
    tags = ["no_windows"],
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_library(
    name = "oauth2_utils",
    srcs = ["oauth2_utils.cc"],
    hdrs = ["oauth2_utils.h"],
    language = "C++",
    visibility = ["//test/cpp:__subpackages__"],
    deps = ["//:grpc"],
)

grpc_cc_test(
    name = "auth_context_test",
    srcs = ["auth_context_test.cc"],
    language = "C++",
    uses_polling = False,
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "cel_authorization_engine_test",
    srcs = ["cel_authorization_engine_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc_cel_engine",
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
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "aws_request_signer_test",
    srcs = ["aws_request_signer_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc_secure",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "evaluate_args_test",
    srcs = ["evaluate_args_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc_rbac_engine",
        "//test/core/util:grpc_test_util",
        "//test/core/util:grpc_test_util_base",
    ],
)

grpc_cc_test(
    name = "json_token_test",
    srcs = ["json_token_test.cc"],
    language = "C++",
    uses_polling = False,
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "jwt_verifier_test",
    srcs = ["jwt_verifier_test.cc"],
    language = "C++",
    uses_polling = False,
    deps = [
        "//:gpr",
        "//:grpc",
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
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "linux_system_roots_test",
    srcs = ["linux_system_roots_test.cc"],
    data = [
        "//test/core/security/etc:bundle.pem",
        "//test/core/security/etc:test_roots/cert1.pem",
        "//test/core/security/etc:test_roots/cert2.pem",
        "//test/core/security/etc:test_roots/cert3.pem",
    ],
    external_deps = [
        "gtest",
    ],
    language = "C++",
    deps = [
        "//:gpr",
        "//:grpc",
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
        "//test/core/util:grpc_test_util",
    ],
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
        "//:grpc++",
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
        "//test/core/util:grpc_test_util",
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
        "//test/core/util:grpc_test_util",
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
        "//test/core/util:grpc_test_util",
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
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "tls_security_connector_test",
    srcs = ["tls_security_connector_test.cc"],
    data = [
        "//src/core/tsi/test_creds:ca.pem",
        "//src/core/tsi/test_creds:multi-domain.key",
        "//src/core/tsi/test_creds:multi-domain.pem",
        "//src/core/tsi/test_creds:server0.key",
        "//src/core/tsi/test_creds:server0.pem",
        "//src/core/tsi/test_creds:server1.key",
        "//src/core/tsi/test_creds:server1.pem",
    ],
    external_deps = [
        "gtest",
    ],
    language = "C++",
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc_secure",
        "//:tsi",
        "//:tsi_interface",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "grpc_tls_credentials_options_test",
    srcs = ["grpc_tls_credentials_options_test.cc"],
    data = [
        "//src/core/tsi/test_creds:ca.pem",
        "//src/core/tsi/test_creds:multi-domain.key",
        "//src/core/tsi/test_creds:multi-domain.pem",
        "//src/core/tsi/test_creds:server0.key",
        "//src/core/tsi/test_creds:server0.pem",
        "//src/core/tsi/test_creds:server1.key",
        "//src/core/tsi/test_creds:server1.pem",
    ],
    external_deps = ["gtest"],
    language = "C++",
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc_secure",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "grpc_tls_certificate_distributor_test",
    srcs = ["grpc_tls_certificate_distributor_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc_secure",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "grpc_tls_certificate_provider_test",
    srcs = ["grpc_tls_certificate_provider_test.cc"],
    data = [
        "//src/core/tsi/test_creds:ca.pem",
        "//src/core/tsi/test_creds:multi-domain.key",
        "//src/core/tsi/test_creds:multi-domain.pem",
        "//src/core/tsi/test_creds:server0.key",
        "//src/core/tsi/test_creds:server0.pem",
        "//src/core/tsi/test_creds:server1.key",
        "//src/core/tsi/test_creds:server1.pem",
    ],
    external_deps = ["gtest"],
    language = "C++",
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc_secure",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "insecure_security_connector_test",
    srcs = ["insecure_security_connector_test.cc"],
    external_deps = [
        "gtest",
    ],
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc_secure",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "xds_credentials_test",
    srcs = ["xds_credentials_test.cc"],
    external_deps = [
        "gtest",
    ],
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc_secure",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "matchers_test",
    srcs = ["matchers_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc_secure",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "rbac_translator_test",
    srcs = ["rbac_translator_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc_authorization_provider",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "authorization_matchers_test",
    srcs = ["authorization_matchers_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc_rbac_engine",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "grpc_authorization_engine_test",
    srcs = ["grpc_authorization_engine_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc_rbac_engine",
        "//test/core/util:grpc_test_util",
    ],
)
