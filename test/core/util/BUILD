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
    name = "gpr_test_util",
    srcs = [
        "test_config.c",
        "memory_counters.c",
    ],
    hdrs = [
        "test_config.h",
        "memory_counters.h",
    ],
    deps = ["//:gpr"],
    visibility = ["//:__subpackages__"],
)

cc_library(
    name = "grpc_test_util",
    srcs = [
        "debugger_macros.c",
        "grpc_profiler.c",
        "mock_endpoint.c",
        "parse_hexstring.c",
        "passthru_endpoint.c",
        "port.c",
        "port_server_client.c",
        "reconnect_server.c",
        "slice_splitter.c",
        "test_tcp_server.c",
        "trickle_endpoint.c",
    ],
    hdrs = [
        "debugger_macros.h",
        "trickle_endpoint.h",
        "grpc_profiler.h",
        "mock_endpoint.h",
        "parse_hexstring.h",
        "passthru_endpoint.h",
        "port.h",
        "port_server_client.h",
        "reconnect_server.h",
        "slice_splitter.h",
        "test_tcp_server.h",
    ],
    deps = [":gpr_test_util", "//:grpc"],
    visibility = ["//test:__subpackages__"],
    copts = ["-std=c99"],
)

cc_library(
  name = "one_corpus_entry_fuzzer",
  srcs = ["one_corpus_entry_fuzzer.c"],
  deps = [":gpr_test_util", "//:grpc"],
  visibility = ["//test:__subpackages__"],
)

sh_library(
  name = "fuzzer_one_entry_runner",
  srcs = ["fuzzer_one_entry_runner.sh"],
  visibility = ["//test:__subpackages__"],
)
