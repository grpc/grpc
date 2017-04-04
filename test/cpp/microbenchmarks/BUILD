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

cc_test(
    name = "noop-benchmark",
    srcs = ["noop-benchmark.cc"],
    deps = ["//external:benchmark"],
    linkopts = ["-pthread"],
)

cc_library(
    name = "helpers",
    srcs = ["helpers.cc"],
    hdrs = ["helpers.h", "fullstack_fixtures.h", "fullstack_context_mutators.h"],
    deps = ["//:grpc++", "//external:benchmark", "//test/core/util:grpc_test_util", "//src/proto/grpc/testing:echo_proto"],
    linkopts = ["-pthread"],
)

cc_test(
    name = "bm_closure",
    srcs = ["bm_closure.cc"],
    deps = [":helpers"],
)

cc_test(
    name = "bm_cq",
    srcs = ["bm_cq.cc"],
    deps = [":helpers"],
)

cc_test(
    name = "bm_error",
    srcs = ["bm_error.cc"],
    deps = [":helpers"],
)

cc_test(
    name = "bm_fullstack_streaming_ping_pong",
    srcs = ["bm_fullstack_streaming_ping_pong.cc"],
    deps = [":helpers"],

    )
cc_test(
    name = "bm_fullstack_streaming_pump",
    srcs = ["bm_fullstack_streaming_pump.cc"],
    deps = [":helpers"],
)

cc_test(
    name = "bm_fullstack_trickle",
    srcs = ["bm_fullstack_trickle.cc"],
    deps = [":helpers"],
)

cc_test(
    name = "bm_fullstack_unary_ping_pong",
    srcs = ["bm_fullstack_unary_ping_pong.cc"],
    deps = [":helpers"],
)

cc_test(
    name = "bm_metadata",
    srcs = ["bm_metadata.cc"],
    deps = [":helpers"],
)
