# Copyright 2017, gRPC authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

licenses(["notice"])  # Apache v2

load("//test/core/util:grpc_fuzzer.bzl", "grpc_fuzzer")

grpc_fuzzer(
  name = "json_fuzzer",
  srcs = ["fuzzer.c"],
  deps = ["//:gpr", "//:grpc", "//test/core/util:grpc_test_util"],
  corpus = "corpus",
  copts = ["-std=c99"],
)

cc_binary(
    name = "json_rewrite",
    srcs = ["json_rewrite.c"],
    deps = ["//:grpc", "//test/core/util:grpc_test_util", "//:gpr", "//test/core/util:gpr_test_util"],
    testonly = 1,
    copts = ['-std=c99']
)

cc_test(
    name = "json_rewrite_test",
    srcs = ["json_rewrite_test.c"],
    deps = ["//:grpc", "//test/core/util:grpc_test_util", "//:gpr", "//test/core/util:gpr_test_util"],
    copts = ['-std=c99'],
    data = ["rewrite_test_input.json", "rewrite_test_output_condensed.json", "rewrite_test_output_indented.json", ":json_stream_error_test"]
)

cc_test(
    name = "json_stream_error_test",
    srcs = ["json_stream_error_test.c"],
    deps = ["//:grpc", "//test/core/util:grpc_test_util", "//:gpr", "//test/core/util:gpr_test_util"],
    copts = ['-std=c99']
)

cc_test(
    name = "json_test",
    srcs = ["json_test.c"],
    deps = ["//:grpc", "//test/core/util:grpc_test_util", "//:gpr", "//test/core/util:gpr_test_util"],
    copts = ['-std=c99']
)
