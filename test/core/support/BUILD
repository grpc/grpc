# Copyright 2016, Google Inc.
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

load("//bazel:grpc_build_system.bzl", "grpc_cc_library", "grpc_cc_test", "grpc_cc_binary")

licenses(["notice"])  # 3-clause BSD

package(
    features = [
        "-layering_check",
        "-parse_headers",
    ],
)

grpc_cc_test(
    name = "alloc_test",
    srcs = ["alloc_test.c"],
    language = "C",
    deps = [
        "//:gpr",
        "//test/core/util:gpr_test_util",
    ],
)

grpc_cc_test(
    name = "avl_test",
    srcs = ["avl_test.c"],
    language = "C",
    deps = [
        "//:gpr",
        "//test/core/util:gpr_test_util",
    ],
)

grpc_cc_test(
    name = "backoff_test",
    srcs = ["backoff_test.c"],
    language = "C",
    deps = [
        "//:gpr",
        "//test/core/util:gpr_test_util",
    ],
)

grpc_cc_test(
    name = "cmdline_test",
    srcs = ["cmdline_test.c"],
    language = "C",
    deps = [
        "//:gpr",
        "//test/core/util:gpr_test_util",
    ],
)

grpc_cc_test(
    name = "cpu_test",
    srcs = ["cpu_test.c"],
    language = "C",
    deps = [
        "//:gpr",
        "//test/core/util:gpr_test_util",
    ],
)

grpc_cc_test(
    name = "env_test",
    srcs = ["env_test.c"],
    language = "C",
    deps = [
        "//:gpr",
        "//test/core/util:gpr_test_util",
    ],
)

grpc_cc_test(
    name = "histogram_test",
    srcs = ["histogram_test.c"],
    language = "C",
    deps = [
        "//:gpr",
        "//test/core/util:gpr_test_util",
    ],
)

grpc_cc_test(
    name = "host_port_test",
    srcs = ["host_port_test.c"],
    language = "C",
    deps = [
        "//:gpr",
        "//test/core/util:gpr_test_util",
    ],
)

grpc_cc_test(
    name = "log_test",
    srcs = ["log_test.c"],
    language = "C",
    deps = [
        "//:gpr",
        "//test/core/util:gpr_test_util",
    ],
)

grpc_cc_test(
    name = "mpscq_test",
    srcs = ["mpscq_test.c"],
    language = "C",
    deps = [
        "//:gpr",
        "//test/core/util:gpr_test_util",
    ],
)

grpc_cc_test(
    name = "murmur_hash_test",
    srcs = ["murmur_hash_test.c"],
    language = "C",
    deps = [
        "//:gpr",
        "//test/core/util:gpr_test_util",
    ],
)

grpc_cc_test(
    name = "stack_lockfree_test",
    srcs = ["stack_lockfree_test.c"],
    language = "C",
    deps = [
        "//:gpr",
        "//test/core/util:gpr_test_util",
    ],
)

grpc_cc_test(
    name = "string_test",
    srcs = ["string_test.c"],
    language = "C",
    deps = [
        "//:gpr",
        "//test/core/util:gpr_test_util",
    ],
)

grpc_cc_test(
    name = "spinlock_test",
    srcs = ["spinlock_test.c"],
    language = "C",
    deps = [
        "//:gpr",
        "//test/core/util:gpr_test_util",
    ],
)

grpc_cc_test(
    name = "sync_test",
    srcs = ["sync_test.c"],
    language = "C",
    deps = [
        "//:gpr",
        "//test/core/util:gpr_test_util",
    ],
)

grpc_cc_test(
    name = "thd_test",
    srcs = ["thd_test.c"],
    language = "C",
    deps = [
        "//:gpr",
        "//test/core/util:gpr_test_util",
    ],
)

grpc_cc_test(
    name = "time_test",
    srcs = ["time_test.c"],
    language = "C",
    deps = [
        "//:gpr",
        "//test/core/util:gpr_test_util",
    ],
)

grpc_cc_test(
    name = "tls_test",
    srcs = ["tls_test.c"],
    language = "C",
    deps = [
        "//:gpr",
        "//test/core/util:gpr_test_util",
    ],
)

grpc_cc_test(
    name = "useful_test",
    srcs = ["useful_test.c"],
    language = "C",
    deps = [
        "//:gpr",
        "//test/core/util:gpr_test_util",
    ],
)
