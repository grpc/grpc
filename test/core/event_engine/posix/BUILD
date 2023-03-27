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

load("//bazel:grpc_build_system.bzl", "grpc_cc_library", "grpc_cc_test", "grpc_package")

licenses(["notice"])

grpc_package(
    name = "test/core/event_engine/posix",
    visibility = "public",
)  # Useful for third party devs to test their io manager implementation.

grpc_cc_library(
    name = "posix_engine_test_utils",
    testonly = True,
    srcs = ["posix_engine_test_utils.cc"],
    hdrs = ["posix_engine_test_utils.h"],
    tags = [
        "no_windows",
    ],
    visibility = ["//test:__subpackages__"],
    deps = [
        "//src/core:event_engine_common",
        "//src/core:posix_event_engine_event_poller",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "timer_heap_test",
    srcs = ["timer_heap_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//src/core:bitset",
        "//src/core:posix_event_engine_timer",
    ],
)

grpc_cc_test(
    name = "timer_list_test",
    srcs = ["timer_list_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//src/core:posix_event_engine_timer",
    ],
)

grpc_cc_test(
    name = "timer_manager_test",
    srcs = ["timer_manager_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:exec_ctx",
        "//src/core:common_event_engine_closures",
        "//src/core:posix_event_engine_timer_manager",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "event_poller_posix_test",
    srcs = ["event_poller_posix_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    tags = [
        "no_windows",
    ],
    uses_event_engine = True,
    uses_polling = True,
    deps = [
        "//src/core:common_event_engine_closures",
        "//src/core:event_engine_poller",
        "//src/core:posix_event_engine",
        "//src/core:posix_event_engine_closure",
        "//src/core:posix_event_engine_event_poller",
        "//src/core:posix_event_engine_poller_posix_default",
        "//test/core/event_engine/posix:posix_engine_test_utils",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "lock_free_event_test",
    srcs = ["lock_free_event_test.cc"],
    external_deps = [
        "benchmark",
        "gtest",
    ],
    language = "C++",
    tags = [
        "no_mac",
        "no_windows",
    ],
    uses_event_engine = True,
    uses_polling = False,
    deps = [
        "//src/core:posix_event_engine",
        "//src/core:posix_event_engine_closure",
        "//src/core:posix_event_engine_event_poller",
        "//src/core:posix_event_engine_lockfree_event",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "wakeup_fd_posix_test",
    srcs = ["wakeup_fd_posix_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    tags = [
        "no_windows",
    ],
    uses_event_engine = False,
    uses_polling = True,
    deps = [
        "//src/core:posix_event_engine_wakeup_fd_posix_eventfd",
        "//src/core:posix_event_engine_wakeup_fd_posix_pipe",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "traced_buffer_list_test",
    srcs = ["traced_buffer_list_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    tags = [
        "no_windows",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//src/core:posix_event_engine_traced_buffer_list",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "tcp_posix_socket_utils_test",
    srcs = ["tcp_posix_socket_utils_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    tags = [
        "no_windows",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//src/core:event_engine_common",
        "//src/core:posix_event_engine_tcp_socket_utils",
        "//src/core:socket_mutator",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "posix_endpoint_test",
    srcs = ["posix_endpoint_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    tags = [
        "no_windows",
    ],
    uses_event_engine = True,
    uses_polling = True,
    deps = [
        "//src/core:channel_args",
        "//src/core:common_event_engine_closures",
        "//src/core:event_engine_poller",
        "//src/core:posix_event_engine",
        "//src/core:posix_event_engine_closure",
        "//src/core:posix_event_engine_endpoint",
        "//src/core:posix_event_engine_event_poller",
        "//src/core:posix_event_engine_poller_posix_default",
        "//test/core/event_engine:event_engine_test_utils",
        "//test/core/event_engine/posix:posix_engine_test_utils",
        "//test/core/event_engine/test_suite/posix:oracle_event_engine_posix",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "posix_engine_listener_utils_test",
    srcs = ["posix_engine_listener_utils_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    tags = [
        "no_windows",
    ],
    uses_event_engine = False,
    deps = [
        "//src/core:event_engine_common",
        "//src/core:event_engine_tcp_socket_utils",
        "//src/core:posix_event_engine_listener_utils",
        "//src/core:posix_event_engine_tcp_socket_utils",
        "//src/core:socket_mutator",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "posix_event_engine_connect_test",
    srcs = ["posix_event_engine_connect_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    tags = [
        "no_windows",
    ],
    uses_event_engine = True,
    uses_polling = True,
    deps = [
        "//src/core:channel_args",
        "//src/core:event_engine_poller",
        "//src/core:posix_event_engine",
        "//src/core:posix_event_engine_closure",
        "//src/core:posix_event_engine_endpoint",
        "//src/core:posix_event_engine_event_poller",
        "//src/core:posix_event_engine_poller_posix_default",
        "//test/core/event_engine:event_engine_test_utils",
        "//test/core/event_engine/test_suite/posix:oracle_event_engine_posix",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "log_too_many_open_files_test",
    srcs = ["log_too_many_open_files_test.cc"],
    external_deps = [
        "gtest",
    ],
    language = "C++",
    tags = [
        "no_windows",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:grpc",
        "//src/core:posix_event_engine",
        "//test/core/util:grpc_test_util",
    ],
)
