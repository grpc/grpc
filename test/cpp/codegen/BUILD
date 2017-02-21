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
    name = "codegen_test_full",
    srcs = ["codegen_test_full.cc"],
    deps = [
        "//:grpc++",
        "//external:gtest",
        "//test/core/util:gpr_test_util",
    ],
)

cc_test(
    name = "codegen_test_minimal",
    srcs = ["codegen_test_minimal.cc"],
    deps = [
        "//:grpc++",
        "//external:gtest",
        "//test/core/util:gpr_test_util",
    ],
)

cc_test(
    name = "proto_utils_test",
    srcs = ["proto_utils_test.cc"],
    deps = [
        "//:grpc++",
        "//external:gtest",
        "//test/core/util:gpr_test_util",
    ],
)

cc_test(
    name = "golden_file_test",
    srcs = ["golden_file_test.cc"],
    args = ["--generated_file_path=$(GENDIR)/src/proto/grpc/testing/compiler_test.grpc.pb.h"],
    data = [
        ":compiler_test_golden",
        "//src/proto/grpc/testing:_compiler_test_proto_grpc_codegen",
    ],
    deps = [
        "//:grpc++",
        "//external:gflags",
        "//external:gtest",
        "//src/proto/grpc/testing:compiler_test_proto",
        "//test/core/util:gpr_test_util",
    ],
)
