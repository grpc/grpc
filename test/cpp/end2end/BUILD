# Copyright 2017, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

licenses(["notice"])  # 3-clause BSD

cc_library(
    name = "test_service_impl",
    srcs = ["test_service_impl.cc"],
    hdrs = ["test_service_impl.h"],
    deps = [
        "//external:gtest",
        "//src/proto/grpc/testing:echo_proto",
        "//test/cpp/util:test_util",
    ],
)

cc_test(
    name = "async_end2end_test",
    srcs = ["async_end2end_test.cc"],
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//external:gtest",
        "//src/proto/grpc/testing:echo_messages_proto",
        "//src/proto/grpc/testing:echo_proto",
        "//src/proto/grpc/testing/duplicate:echo_duplicate_proto",
        "//test/core/util:gpr_test_util",
        "//test/core/util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)

cc_test(
    name = "client_crash_test",
    srcs = ["client_crash_test.cc"],
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//external:gtest",
        "//src/proto/grpc/testing:echo_messages_proto",
        "//src/proto/grpc/testing:echo_proto",
        "//src/proto/grpc/testing/duplicate:echo_duplicate_proto",
        "//test/core/util:gpr_test_util",
        "//test/core/util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)

cc_test(
    name = "client_crash_test_server",
    srcs = ["client_crash_test_server.cc"],
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//external:gflags",
        "//external:gtest",
        "//src/proto/grpc/testing:echo_messages_proto",
        "//src/proto/grpc/testing:echo_proto",
        "//src/proto/grpc/testing/duplicate:echo_duplicate_proto",
        "//test/core/util:gpr_test_util",
        "//test/core/util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)

cc_test(
    name = "end2end_test",
    srcs = ["end2end_test.cc"],
    deps = [
        ":test_service_impl",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//external:gtest",
        "//src/proto/grpc/testing:echo_messages_proto",
        "//src/proto/grpc/testing:echo_proto",
        "//src/proto/grpc/testing/duplicate:echo_duplicate_proto",
        "//test/core/util:gpr_test_util",
        "//test/core/util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)

cc_test(
    name = "filter_end2end_test",
    srcs = ["filter_end2end_test.cc"],
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//external:gtest",
        "//src/proto/grpc/testing:echo_messages_proto",
        "//src/proto/grpc/testing:echo_proto",
        "//src/proto/grpc/testing/duplicate:echo_duplicate_proto",
        "//test/core/util:gpr_test_util",
        "//test/core/util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)

cc_test(
    name = "generic_end2end_test",
    srcs = ["generic_end2end_test.cc"],
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//external:gtest",
        "//src/proto/grpc/testing:echo_messages_proto",
        "//src/proto/grpc/testing:echo_proto",
        "//src/proto/grpc/testing/duplicate:echo_duplicate_proto",
        "//test/core/util:gpr_test_util",
        "//test/core/util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)

cc_test(
    name = "hybrid_end2end_test",
    srcs = ["hybrid_end2end_test.cc"],
    deps = [
        ":test_service_impl",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//external:gtest",
        "//src/proto/grpc/testing:echo_messages_proto",
        "//src/proto/grpc/testing:echo_proto",
        "//src/proto/grpc/testing/duplicate:echo_duplicate_proto",
        "//test/core/util:gpr_test_util",
        "//test/core/util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)

cc_test(
    name = "mock_test",
    srcs = ["mock_test.cc"],
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//external:gtest",
        "//src/proto/grpc/testing:echo_messages_proto",
        "//src/proto/grpc/testing:echo_proto",
        "//src/proto/grpc/testing/duplicate:echo_duplicate_proto",
        "//test/core/util:gpr_test_util",
        "//test/core/util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)

cc_test(
    name = "round_robin_end2end_test",
    srcs = ["round_robin_end2end_test.cc"],
    deps = [
        ":test_service_impl",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//external:gtest",
        "//src/proto/grpc/testing:echo_messages_proto",
        "//src/proto/grpc/testing:echo_proto",
        "//src/proto/grpc/testing/duplicate:echo_duplicate_proto",
        "//test/core/util:gpr_test_util",
        "//test/core/util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)

cc_test(
    name = "proto_server_reflection_test",
    srcs = ["proto_server_reflection_test.cc"],
    deps = [
        ":test_service_impl",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//:grpc++_reflection",
        "//external:gflags",
        "//external:gtest",
        "//src/proto/grpc/testing:echo_messages_proto",
        "//src/proto/grpc/testing:echo_proto",
        "//src/proto/grpc/testing/duplicate:echo_duplicate_proto",
        "//test/core/util:gpr_test_util",
        "//test/core/util:grpc_test_util",
        "//test/cpp/util:grpc++_proto_reflection_desc_db",
        "//test/cpp/util:test_util",
    ],
)

cc_test(
    name = "server_builder_plugin_test",
    srcs = ["server_builder_plugin_test.cc"],
    deps = [
        ":test_service_impl",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//external:gtest",
        "//src/proto/grpc/testing:echo_messages_proto",
        "//src/proto/grpc/testing:echo_proto",
        "//src/proto/grpc/testing/duplicate:echo_duplicate_proto",
        "//test/core/util:gpr_test_util",
        "//test/core/util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)

cc_test(
    name = "server_crash_test",
    srcs = ["server_crash_test.cc"],
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//external:gtest",
        "//src/proto/grpc/testing:echo_messages_proto",
        "//src/proto/grpc/testing:echo_proto",
        "//src/proto/grpc/testing/duplicate:echo_duplicate_proto",
        "//test/core/util:gpr_test_util",
        "//test/core/util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)

cc_test(
    name = "server_crash_test_client",
    srcs = ["server_crash_test_client.cc"],
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//external:gflags",
        "//external:gtest",
        "//src/proto/grpc/testing:echo_messages_proto",
        "//src/proto/grpc/testing:echo_proto",
        "//src/proto/grpc/testing/duplicate:echo_duplicate_proto",
        "//test/core/util:gpr_test_util",
        "//test/core/util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)

cc_test(
    name = "shutdown_test",
    srcs = ["shutdown_test.cc"],
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//external:gtest",
        "//src/proto/grpc/testing:echo_messages_proto",
        "//src/proto/grpc/testing:echo_proto",
        "//src/proto/grpc/testing/duplicate:echo_duplicate_proto",
        "//test/core/util:gpr_test_util",
        "//test/core/util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)

cc_test(
    name = "streaming_throughput_test",
    srcs = ["streaming_throughput_test.cc"],
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//external:gtest",
        "//src/proto/grpc/testing:echo_messages_proto",
        "//src/proto/grpc/testing:echo_proto",
        "//src/proto/grpc/testing/duplicate:echo_duplicate_proto",
        "//test/core/util:gpr_test_util",
        "//test/core/util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)

cc_test(
    name = "thread_stress_test",
    srcs = ["thread_stress_test.cc"],
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//external:gtest",
        "//src/proto/grpc/testing:echo_messages_proto",
        "//src/proto/grpc/testing:echo_proto",
        "//src/proto/grpc/testing/duplicate:echo_duplicate_proto",
        "//test/core/util:gpr_test_util",
        "//test/core/util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)
