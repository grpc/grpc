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

licenses(["notice"])

grpc_package(name = "test/core/security")

grpc_cc_test(
    name = "cel_authorization_engine_test",
    srcs = ["cel_authorization_engine_test.cc"],
    external_deps = ["gtest"],
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc_cel_engine",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
    ],
)

grpc_cc_test(
    name = "evaluate_args_test",
    srcs = ["evaluate_args_test.cc"],
    external_deps = ["gtest"],
    deps = [
        "//:gpr",
        "//:grpc",
        "//src/core:grpc_authorization_base",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
    ],
)

grpc_cc_test(
    name = "rbac_translator_test",
    srcs = ["rbac_translator_test.cc"],
    external_deps = ["gtest"],
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc_authorization_provider",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
    ],
)

grpc_cc_test(
    name = "authorization_matchers_test",
    srcs = ["authorization_matchers_test.cc"],
    external_deps = ["gtest"],
    deps = [
        "//:gpr",
        "//:grpc",
        "//src/core:grpc_rbac_engine",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
    ],
)

grpc_cc_test(
    name = "grpc_authorization_engine_test",
    srcs = ["grpc_authorization_engine_test.cc"],
    external_deps = ["gtest"],
    deps = [
        "//:gpr",
        "//:grpc",
        "//src/core:grpc_rbac_engine",
        "//test/core/test_util:audit_logging_utils",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
    ],
)

grpc_cc_test(
    name = "grpc_authorization_policy_provider_test",
    srcs = ["grpc_authorization_policy_provider_test.cc"],
    data = [
        "//test/core/security/authorization/test_policies:invalid_policy.json",
        "//test/core/security/authorization/test_policies:valid_policy_1.json",
        "//test/core/security/authorization/test_policies:valid_policy_2.json",
    ],
    external_deps = ["gtest"],
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc_authorization_provider",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
    ],
)

grpc_cc_test(
    name = "grpc_audit_logging_test",
    srcs = ["grpc_audit_logging_test.cc"],
    external_deps = ["gtest"],
    deps = [
        "//:gpr",
        "//:grpc",
        "//src/core:grpc_audit_logging",
        "//test/core/test_util:grpc_test_util",
    ],
)
