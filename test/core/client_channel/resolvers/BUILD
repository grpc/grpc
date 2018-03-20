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

load("//bazel:grpc_build_system.bzl", "grpc_cc_library", "grpc_cc_test", "grpc_cc_binary", "grpc_package")

grpc_package(name = "test/core/client_channel_resolvers")

licenses(["notice"])  # Apache v2

grpc_cc_test(
    name = "dns_resolver_connectivity_test",
    srcs = ["dns_resolver_connectivity_test.cc"],
    language = "C++",
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:gpr_test_util",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "dns_resolver_test",
    srcs = ["dns_resolver_test.cc"],
    language = "C++",
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:gpr_test_util",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "dns_resolver_cooldown_test",
    srcs = ["dns_resolver_cooldown_test.cc"],
    language = "C++",
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:gpr_test_util",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "sockaddr_resolver_test",
    srcs = ["sockaddr_resolver_test.cc"],
    language = "C++",
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:gpr_test_util",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "fake_resolver_test",
    srcs = ["fake_resolver_test.cc"],
    language = "C++",
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc_resolver_fake",
        "//test/core/util:gpr_test_util",
        "//test/core/util:grpc_test_util",
    ],
)
