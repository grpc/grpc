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

load("//bazel:grpc_build_system.bzl", "grpc_cc_test", "grpc_package")

grpc_package(name = "test/core/client_channel/resolvers")

licenses(["notice"])

grpc_cc_test(
    name = "binder_resolver_test",
    srcs = ["binder_resolver_test.cc"],
    external_deps = [
        "gtest",
    ],
    language = "C++",
    deps = [
        "//:gpr",
        "//:grpc",
        "//src/core:grpc_resolver_binder",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "dns_resolver_test",
    srcs = ["dns_resolver_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "dns_resolver_cooldown_test",
    srcs = ["dns_resolver_cooldown_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    deps = [
        "//:gpr",
        "//:grpc",
        "//src/core:channel_args",
        "//src/core:closure",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "sockaddr_resolver_test",
    srcs = ["sockaddr_resolver_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "fake_resolver_test",
    srcs = ["fake_resolver_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc_resolver_fake",
        "//src/core:channel_args",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "google_c2p_resolver_test",
    srcs = ["google_c2p_resolver_test.cc"],
    external_deps = [
        "absl/strings:str_format",
        "gtest",
    ],
    language = "C++",
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//test/core/util:fake_udp_and_tcp_server",
        "//test/core/util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)
