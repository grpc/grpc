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

load("//bazel:grpc_build_system.bzl", "grpc_cc_binary", "grpc_cc_library", "grpc_cc_test", "grpc_package")
load("//bazel:custom_exec_properties.bzl", "LARGE_MACHINE")

licenses(["notice"])  # Apache v2

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
