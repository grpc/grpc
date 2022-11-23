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

load("//bazel:grpc_build_system.bzl", "grpc_cc_library", "grpc_cc_test", "grpc_package")

grpc_package(name = "test/core/handshake")

licenses(["notice"])

grpc_cc_test(
    name = "client_ssl_test",
    srcs = ["client_ssl.cc"],
    data = [
        "//src/core/tsi/test_creds:ca.pem",
        "//src/core/tsi/test_creds:server1.key",
        "//src/core/tsi/test_creds:server1.pem",
    ],
    external_deps = ["gtest"],
    language = "C++",
    tags = ["no_windows"],
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_library(
    name = "server_ssl_common",
    srcs = ["server_ssl_common.cc"],
    hdrs = ["server_ssl_common.h"],
    tags = ["no_windows"],
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "server_ssl_test",
    srcs = ["server_ssl.cc"],
    data = [
        "//src/core/tsi/test_creds:ca.pem",
        "//src/core/tsi/test_creds:server1.key",
        "//src/core/tsi/test_creds:server1.pem",
    ],
    external_deps = ["gtest"],
    language = "C++",
    tags = ["no_windows"],
    deps = [
        ":server_ssl_common",
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "handshake_server_with_readahead_handshaker_test",
    srcs = ["readahead_handshaker_server_ssl.cc"],
    data = [
        "//src/core/tsi/test_creds:ca.pem",
        "//src/core/tsi/test_creds:server1.key",
        "//src/core/tsi/test_creds:server1.pem",
    ],
    external_deps = ["gtest"],
    language = "C++",
    tags = ["no_windows"],
    deps = [
        ":server_ssl_common",
        "//:gpr",
        "//:grpc",
        "//src/core:channel_args",
        "//src/core:closure",
        "//test/core/util:grpc_test_util",
    ],
)

# Disabled as per b/178094682
#grpc_cc_test(
#    name = "handshake_verify_peer_options_test",
#    srcs = ["verify_peer_options.cc"],
#    data = [
#        "//src/core/tsi/test_creds:ca.pem",
#        "//src/core/tsi/test_creds:server1.key",
#        "//src/core/tsi/test_creds:server1.pem",
#    ],
#    language = "C++",
#    tags = ["no_mac", no_windows"],
#    deps = [
#        "//:gpr",
#        "//:grpc",
#        "//test/core/util:grpc_test_util",
#    ],
#)
