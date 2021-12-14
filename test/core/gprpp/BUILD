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

load("//bazel:grpc_build_system.bzl", "grpc_cc_test", "grpc_package")
load("//bazel:custom_exec_properties.bzl", "LARGE_MACHINE")
load("//test/core/util:grpc_fuzzer.bzl", "grpc_proto_fuzzer")

licenses(["notice"])

grpc_package(name = "test/core/gprpp")

grpc_cc_test(
    name = "examine_stack_test",
    srcs = ["examine_stack_test.cc"],
    external_deps = [
        "absl/debugging:stacktrace",
        "absl/debugging:symbolize",
        "gtest",
    ],
    language = "C++",
    # TODO(https://github.com/grpc/grpc/issues/24627): Disable this on Windows
    tags = ["no_windows"],
    uses_polling = False,
    deps = [
        "//:gpr",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "fork_test",
    srcs = ["fork_test.cc"],
    language = "C++",
    tags = ["no_windows"],
    uses_polling = False,
    deps = [
        "//:gpr",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "global_config_test",
    srcs = ["global_config_test.cc"],
    external_deps = [
        "gtest",
    ],
    language = "C++",
    uses_polling = False,
    deps = [
        "//:gpr",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "global_config_env_test",
    srcs = ["global_config_env_test.cc"],
    external_deps = [
        "gtest",
    ],
    language = "C++",
    tags = ["no_windows"],  # TODO(jtattermusch): fix the failure on windows
    uses_polling = False,
    deps = [
        "//:gpr",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "manual_constructor_test",
    srcs = ["manual_constructor_test.cc"],
    language = "C++",
    uses_polling = False,
    deps = [
        "//:gpr",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "bitset_test",
    srcs = ["bitset_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    uses_polling = False,
    deps = [
        "//:bitset",
        "//test/core/util:grpc_suppressions",
    ],
)

grpc_cc_test(
    name = "match_test",
    srcs = ["match_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    uses_polling = False,
    deps = [
        "//:match",
        "//test/core/util:grpc_suppressions",
    ],
)

grpc_cc_test(
    name = "overload_test",
    srcs = ["overload_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    uses_polling = False,
    deps = [
        "//:overload",
        "//test/core/util:grpc_suppressions",
    ],
)

grpc_cc_test(
    name = "table_test",
    srcs = ["table_test.cc"],
    external_deps = [
        "gtest",
        "absl/types:optional",
    ],
    language = "C++",
    uses_polling = False,
    deps = [
        "//:table",
        "//test/core/util:grpc_suppressions",
    ],
)

grpc_cc_test(
    name = "host_port_test",
    srcs = ["host_port_test.cc"],
    language = "C++",
    uses_polling = False,
    deps = [
        "//:gpr",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "mpscq_test",
    srcs = ["mpscq_test.cc"],
    exec_properties = LARGE_MACHINE,
    language = "C++",
    tags = ["no_windows"],  # LARGE_MACHINE is not configured for windows RBE
    uses_polling = False,
    deps = [
        "//:gpr",
        "//test/core/util:grpc_test_util",
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
        "//test/core/util:grpc_test_util",
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
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "dual_ref_counted_test",
    srcs = ["dual_ref_counted_test.cc"],
    external_deps = [
        "gtest",
    ],
    language = "C++",
    deps = [
        "//:dual_ref_counted",
        "//test/core/util:grpc_test_util",
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
        "//:dual_ref_counted",
        "//:ref_counted",
        "//:ref_counted_ptr",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "thd_test",
    srcs = ["thd_test.cc"],
    language = "C++",
    uses_polling = False,
    deps = [
        "//:gpr",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "stat_test",
    srcs = ["stat_test.cc"],
    external_deps = [
        "gtest",
    ],
    language = "C++",
    uses_polling = False,
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "status_helper_test",
    srcs = ["status_helper_test.cc"],
    external_deps = [
        "gtest",
    ],
    language = "C++",
    uses_polling = False,
    deps = [
        "//:gpr",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "time_util_test",
    srcs = ["time_util_test.cc"],
    external_deps = [
        "gtest",
    ],
    language = "C++",
    uses_polling = False,
    deps = [
        "//:gpr",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "capture_test",
    srcs = ["capture_test.cc"],
    external_deps = ["gtest"],
    language = "c++",
    uses_polling = False,
    deps = [
        "//:capture",
        "//test/core/util:grpc_suppressions",
    ],
)

grpc_cc_test(
    name = "cpp_impl_of_test",
    srcs = ["cpp_impl_of_test.cc"],
    external_deps = ["gtest"],
    language = "c++",
    uses_polling = False,
    deps = [
        "//:cpp_impl_of",
        "//test/core/util:grpc_suppressions",
    ],
)

grpc_cc_test(
    name = "chunked_vector_test",
    srcs = ["chunked_vector_test.cc"],
    external_deps = ["gtest"],
    language = "c++",
    uses_polling = False,
    deps = [
        "//:chunked_vector",
        "//:gpr_base",
        "//:resource_quota",
        "//test/core/util:grpc_suppressions",
    ],
)

grpc_proto_fuzzer(
    name = "chunked_vector_fuzzer",
    srcs = ["chunked_vector_fuzzer.cc"],
    corpus = "chunked_vector_corpora",
    language = "C++",
    proto = "chunked_vector_fuzzer.proto",
    tags = ["no_windows"],
    uses_polling = False,
    deps = [
        "//:chunked_vector",
        "//:resource_quota",
        "//test/core/util:grpc_test_util",
    ],
)
