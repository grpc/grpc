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

grpc_package(
    name = "test/core/tsi",
    visibility = "public",
)

grpc_cc_library(
    name = "transport_security_test_lib",
    srcs = ["transport_security_test_lib.cc"],
    hdrs = ["transport_security_test_lib.h"],
    deps = [
        "//:grpc",
        "//:tsi",
    ],
)

grpc_cc_test(
    name = "fake_transport_security_test",
    srcs = ["fake_transport_security_test.cc"],
    language = "C++",
    deps = [
        ":transport_security_test_lib",
        "//:gpr",
        "//:grpc",
        "//:tsi",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "ssl_session_cache_test",
    srcs = ["ssl_session_cache_test.cc"],
    external_deps = [
        "gtest",
    ],
    language = "C++",
    deps = [
        "//:gpr",
        "//:grpc",
        "//:tsi",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "ssl_transport_security_test",
    srcs = ["ssl_transport_security_test.cc"],
    data = [
        "//src/core/tsi/test_creds:badclient.key",
        "//src/core/tsi/test_creds:badclient.pem",
        "//src/core/tsi/test_creds:badserver.key",
        "//src/core/tsi/test_creds:badserver.pem",
        "//src/core/tsi/test_creds:ca.pem",
        "//src/core/tsi/test_creds:client.key",
        "//src/core/tsi/test_creds:client.pem",
        "//src/core/tsi/test_creds:multi-domain.key",
        "//src/core/tsi/test_creds:multi-domain.pem",
        "//src/core/tsi/test_creds:server0.key",
        "//src/core/tsi/test_creds:server0.pem",
        "//src/core/tsi/test_creds:server1.key",
        "//src/core/tsi/test_creds:server1.pem",
    ],
    language = "C++",
    tags = ["no_windows"],
    deps = [
        ":transport_security_test_lib",
        "//:gpr",
        "//:grpc",
        "//:tsi",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "transport_security_test",
    srcs = ["transport_security_test.cc"],
    language = "C++",
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)
