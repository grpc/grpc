# Copyright 2016, gRPC authors
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
  name = "api_fuzzer",
  srcs = ["api_fuzzer.c"],
  deps = ["//:gpr", "//:grpc", "//test/core/util:grpc_test_util", "//test/core/end2end:ssl_test_data"],
  corpus = "api_fuzzer_corpus",
  copts = ["-std=c99"],
)

grpc_fuzzer(
  name = "client_fuzzer",
  srcs = ["client_fuzzer.c"],
  deps = ["//:gpr", "//:grpc", "//test/core/util:grpc_test_util"],
  corpus = "client_fuzzer_corpus",
  copts = ["-std=c99"],
)

grpc_fuzzer(
  name = "server_fuzzer",
  srcs = ["server_fuzzer.c"],
  deps = ["//:gpr", "//:grpc", "//test/core/util:grpc_test_util"],
  corpus = "server_fuzzer_corpus",
  copts = ["-std=c99"],
)
