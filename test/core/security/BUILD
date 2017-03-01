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
  name = "ssl_server_fuzzer",
  srcs = ["ssl_server_fuzzer.c"],
  deps = ["//:gpr", "//:grpc", "//test/core/util:grpc_test_util"],
  corpus = "corpus",
  copts = ["-std=c99"],
)

cc_library(
    name = "oauth2_utils",
    srcs = ["oauth2_utils.c"],
    hdrs = ["oauth2_utils.h"],
    deps = ["//:grpc"],
    copts = ['-std=c99']
)

cc_test(
    name = "auth_context_test",
    srcs = ["auth_context_test.c"],
    deps = ["//:grpc", "//test/core/util:grpc_test_util", "//:gpr", "//test/core/util:gpr_test_util"],
    copts = ['-std=c99']
)

cc_test(
    name = "b64_test",
    srcs = ["b64_test.c"],
    deps = ["//:grpc", "//test/core/util:grpc_test_util", "//:gpr", "//test/core/util:gpr_test_util"],
    copts = ['-std=c99']
)

cc_test(
    name = "credentials_test",
    srcs = ["credentials_test.c"],
    deps = ["//:grpc", "//test/core/util:grpc_test_util", "//:gpr", "//test/core/util:gpr_test_util"],
    copts = ['-std=c99']
)

cc_test(
    name = "secure_endpoint_test",
    srcs = ["secure_endpoint_test.c"],
    deps = ["//:grpc", "//test/core/util:grpc_test_util", "//:gpr", "//test/core/util:gpr_test_util", "//test/core/iomgr:endpoint_tests"],
    copts = ['-std=c99']
)

cc_test(
    name = "security_connector_test",
    srcs = ["security_connector_test.c"],
    deps = ["//:grpc", "//test/core/util:grpc_test_util", "//:gpr", "//test/core/util:gpr_test_util"],
    copts = ['-std=c99']
)

cc_binary(
    name = "create_jwt",
    srcs = ["create_jwt.c"],
    deps = ["//:grpc", "//:gpr"],
    copts = ['-std=c99']
)

cc_binary(
    name = "fetch_oauth2",
    srcs = ["fetch_oauth2.c"],
    deps = ["//:grpc", "//:gpr", ":oauth2_utils"],
    copts = ['-std=c99']
)

cc_binary(
    name = "verify_jwt",
    srcs = ["verify_jwt.c"],
    deps = ["//:grpc", "//:gpr"],
    copts = ['-std=c99']
)
