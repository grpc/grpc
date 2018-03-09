# Copyright 2016 gRPC authors.
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

licenses(["notice"])  # Apache v2

grpc_package(name = "test/core/gpr")

grpc_cc_test(
    name = "alloc_test",
    srcs = ["alloc_test.cc"],
    language = "C++",
    deps = [
        "//:gpr",
        "//test/core/util:gpr_test_util",
    ],
)

grpc_cc_test(
    name = "arena_test",
    srcs = ["arena_test.cc"],
    language = "C++",
    deps = [
        "//:gpr",
        "//test/core/util:gpr_test_util",
    ],
)

grpc_cc_test(
    name = "cpu_test",
    srcs = ["cpu_test.cc"],
    language = "C++",
    deps = [
        "//:gpr",
        "//test/core/util:gpr_test_util",
    ],
)

grpc_cc_test(
    name = "env_test",
    srcs = ["env_test.cc"],
    language = "C++",
    deps = [
        "//:gpr",
        "//test/core/util:gpr_test_util",
    ],
)

grpc_cc_test(
    name = "host_port_test",
    srcs = ["host_port_test.cc"],
    language = "C++",
    deps = [
        "//:gpr",
        "//test/core/util:gpr_test_util",
    ],
)

grpc_cc_test(
    name = "log_test",
    srcs = ["log_test.cc"],
    language = "C++",
    deps = [
        "//:gpr",
        "//test/core/util:gpr_test_util",
    ],
)

grpc_cc_test(
    name = "mpscq_test",
    srcs = ["mpscq_test.cc"],
    language = "C++",
    deps = [
        "//:gpr",
        "//test/core/util:gpr_test_util",
    ],
)

grpc_cc_test(
    name = "murmur_hash_test",
    srcs = ["murmur_hash_test.cc"],
    language = "C++",
    deps = [
        "//:gpr",
        "//test/core/util:gpr_test_util",
    ],
)

grpc_cc_test(
    name = "string_test",
    srcs = ["string_test.cc"],
    language = "C++",
    deps = [
        "//:gpr",
        "//test/core/util:gpr_test_util",
    ],
)

grpc_cc_test(
    name = "spinlock_test",
    srcs = ["spinlock_test.cc"],
    language = "C++",
    deps = [
        "//:gpr",
        "//test/core/util:gpr_test_util",
    ],
)

grpc_cc_test(
    name = "sync_test",
    srcs = ["sync_test.cc"],
    language = "C++",
    deps = [
        "//:gpr",
        "//test/core/util:gpr_test_util",
    ],
)

grpc_cc_test(
    name = "time_test",
    srcs = ["time_test.cc"],
    language = "C++",
    deps = [
        "//:gpr",
        "//test/core/util:gpr_test_util",
    ],
)

grpc_cc_test(
    name = "tls_test",
    srcs = ["tls_test.cc"],
    language = "C++",
    deps = [
        "//:gpr",
        "//test/core/util:gpr_test_util",
    ],
)

grpc_cc_test(
    name = "useful_test",
    srcs = ["useful_test.cc"],
    language = "C++",
    deps = [
        "//:gpr",
        "//test/core/util:gpr_test_util",
    ],
)
