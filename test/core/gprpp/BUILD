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

grpc_package(name = "test/core/gprpp")

grpc_cc_test(
    name = "manual_constructor_test",
    srcs = ["manual_constructor_test.cc"],
    language = "C++",
    deps = [
        "//:gpr",
        "//test/core/util:gpr_test_util",
    ],
)

grpc_cc_test(
    name = "memory_test",
    srcs = ["memory_test.cc"],
    external_deps = [
        "gtest",
    ],
    language = "C++",
    deps = [
        "//:gpr_base",
        "//test/core/util:gpr_test_util",
    ],
)

grpc_cc_test(
    name = "inlined_vector_test",
    srcs = ["inlined_vector_test.cc"],
    external_deps = [
        "gtest",
    ],
    language = "C++",
    deps = [
        "//:inlined_vector",
        "//test/core/util:gpr_test_util",
    ],
)

grpc_cc_test(
    name = "orphanable_test",
    srcs = ["orphanable_test.cc"],
    external_deps = [
        "gtest",
    ],
    language = "C++",
    deps = [
        "//:orphanable",
        "//test/core/util:gpr_test_util",
    ],
)

grpc_cc_test(
    name = "ref_counted_test",
    srcs = ["ref_counted_test.cc"],
    external_deps = [
        "gtest",
    ],
    language = "C++",
    deps = [
        "//:ref_counted",
        "//test/core/util:gpr_test_util",
    ],
)

grpc_cc_test(
    name = "ref_counted_ptr_test",
    srcs = ["ref_counted_ptr_test.cc"],
    external_deps = [
        "gtest",
    ],
    language = "C++",
    deps = [
        "//:ref_counted",
        "//:ref_counted_ptr",
        "//test/core/util:gpr_test_util",
    ],
)

grpc_cc_test(
    name = "thd_test",
    srcs = ["thd_test.cc"],
    language = "C++",
    deps = [
        "//:gpr",
        "//test/core/util:gpr_test_util",
    ],
)
