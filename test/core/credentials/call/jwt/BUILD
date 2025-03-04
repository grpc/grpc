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

load("//bazel:grpc_build_system.bzl", "grpc_cc_binary", "grpc_cc_test", "grpc_package")

licenses(["notice"])

grpc_package(name = "test/core/credentials/call/jwt")

grpc_cc_test(
    name = "json_token_test",
    srcs = ["json_token_test.cc"],
    external_deps = [
        "absl/log:log",
        "gtest",
    ],
    tags = ["credential_token_tests"],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:gpr",
        "//:grpc",
        "//src/core:slice",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
    ],
)

grpc_cc_test(
    name = "jwt_verifier_test",
    srcs = ["jwt_verifier_test.cc"],
    external_deps = ["gtest"],
    tags = ["credential_token_tests"],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
    ],
)

grpc_cc_binary(
    name = "create_jwt",
    srcs = ["create_jwt.cc"],
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
    ],
)

grpc_cc_binary(
    name = "verify_jwt",
    srcs = ["verify_jwt.cc"],
    external_deps = ["absl/log:check"],
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
    ],
)
