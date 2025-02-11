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
load("//test/core/test_util:grpc_fuzzer.bzl", "grpc_fuzz_test")

grpc_package(name = "test/core/handshake")

licenses(["notice"])

grpc_cc_library(
    name = "test_handshake",
    srcs = ["test_handshake.cc"],
    hdrs = ["test_handshake.h"],
    deps = [
        "//:handshaker",
        "//:iomgr",
        "//src/core:channel_args",
        "//src/core:channel_args_endpoint_config",
        "//src/core:event_engine_memory_allocator_factory",
        "//src/core:event_engine_tcp_socket_utils",
        "//src/core:resource_quota",
        "//test/core/event_engine/fuzzing_event_engine",
        "//test/core/event_engine/fuzzing_event_engine:fuzzing_event_engine_cc_proto",
    ],
)

grpc_fuzz_test(
    name = "handshaker_fuzzer",
    srcs = ["handshaker_fuzzer.cc"],
    external_deps = [
        "fuzztest",
        "fuzztest_main",
    ],
    deps = [
        "test_handshake",
        "//test/core/event_engine/fuzzing_event_engine:fuzzing_event_engine_cc_proto",
        "//test/core/test_util:fuzzing_channel_args",
        "//test/core/test_util:fuzzing_channel_args_cc_proto",
    ],
)

grpc_cc_test(
    name = "client_ssl_test",
    srcs = ["client_ssl.cc"],
    data = [
        "//src/core/tsi/test_creds:ca.pem",
        "//src/core/tsi/test_creds:server1.key",
        "//src/core/tsi/test_creds:server1.pem",
    ],
    external_deps = [
        "absl/log:log",
        "gtest",
    ],
    tags = ["no_windows"],
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_library(
    name = "server_ssl_common",
    srcs = ["server_ssl_common.cc"],
    hdrs = ["server_ssl_common.h"],
    external_deps = [
        "absl/log:check",
        "absl/log:log",
    ],
    tags = ["no_windows"],
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/test_util:grpc_test_util",
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
    tags = ["no_windows"],
    deps = [
        ":server_ssl_common",
        "//:gpr",
        "//:grpc",
        "//test/core/test_util:grpc_test_util",
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
    tags = ["no_windows"],
    deps = [
        ":server_ssl_common",
        "//:gpr",
        "//:grpc",
        "//src/core:channel_args",
        "//src/core:closure",
        "//test/core/test_util:grpc_test_util",
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
#    external_deps = [
#        "absl/log:check",
#        "absl/log:log",
#    ],
#    #    tags = ["no_mac", no_windows"],
#    deps = [
#        "//:gpr",
#        "//:grpc",
#        "//test/core/test_util:grpc_test_util",
#    ],
#)

grpc_cc_test(
    name = "http_proxy_mapper_test",
    srcs = ["http_proxy_mapper_test.cc"],
    external_deps = ["gtest"],
    uses_polling = False,
    deps = [
        "//:gpr",
        "//:grpc",
        "//src/core:channel_args",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:scoped_env_var",
    ],
)
