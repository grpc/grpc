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
load("//test/cpp/microbenchmarks:grpc_benchmark_config.bzl", "grpc_cc_benchmark")

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
    external_deps = [
        "absl/log:log",
        "absl/functional:any_invocable",
        "absl/strings",
        "absl/strings:str_format",
    ],
    tags = [
        "no_windows",
    ],
    visibility = ["//visibility:private"],
    deps = [
        "//:event_engine_base_hdrs",
        "//:gpr",
        "//src/core:event_engine_common",
        "//src/core:posix_event_engine_event_poller",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "timer_heap_test",
    srcs = ["timer_heap_test.cc"],
    external_deps = [
        "absl/log:check",
        "gtest",
    ],
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
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:event_engine_base_hdrs",
        "//:gpr",
        "//src/core:posix_event_engine_timer",
        "//src/core:time",
    ],
)

grpc_cc_test(
    name = "timer_manager_test",
    srcs = ["timer_manager_test.cc"],
    external_deps = [
        "absl/functional:any_invocable",
        "absl/log:log",
        "absl/time",
        "gtest",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:exec_ctx",
        "//:grpc",
        "//src/core:common_event_engine_closures",
        "//src/core:event_engine_thread_pool",
        "//src/core:posix_event_engine_timer",
        "//src/core:posix_event_engine_timer_manager",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "event_poller_posix_test",
    srcs = ["event_poller_posix_test.cc"],
    external_deps = [
        "absl/log:log",
        "gtest",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
        "absl/status",
    ],
    tags = [
        "no_windows",
    ],
    uses_event_engine = True,
    uses_polling = True,
    deps = [
        "//:config_vars",
        "//:gpr",
        "//:grpc_base",
        "//:ref_counted_ptr",
        "//src/core:common_event_engine_closures",
        "//src/core:dual_ref_counted",
        "//src/core:event_engine_poller",
        "//src/core:iomgr_port",
        "//src/core:notification",
        "//src/core:posix_event_engine",
        "//src/core:posix_event_engine_closure",
        "//src/core:posix_event_engine_event_poller",
        "//src/core:posix_event_engine_poller_posix_default",
        "//src/core:posix_event_engine_wakeup_fd_posix",
        "//src/core:posix_event_engine_wakeup_fd_posix_pipe",
        "//src/core:strerror",
        "//test/core/event_engine/posix:posix_engine_test_utils",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_benchmark(
    name = "lock_free_event_test",
    srcs = ["lock_free_event_test.cc"],
    external_deps = [
        "gtest",
        "absl/functional:any_invocable",
        "absl/status",
        "absl/time",
    ],
    uses_event_engine = True,
    deps = [
        "//:event_engine_base_hdrs",
        "//:grpc",
        "//src/core:default_event_engine",
        "//src/core:posix_event_engine",
        "//src/core:posix_event_engine_closure",
        "//src/core:posix_event_engine_event_poller",
        "//src/core:posix_event_engine_lockfree_event",
        "//src/core:sync",
        "//test/core/event_engine/posix:posix_engine_test_utils",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "wakeup_fd_posix_test",
    srcs = ["wakeup_fd_posix_test.cc"],
    external_deps = [
        "gtest",
        "absl/status:statusor",
    ],
    tags = [
        "no_windows",
    ],
    uses_event_engine = False,
    uses_polling = True,
    deps = [
        "//src/core:posix_event_engine_wakeup_fd_posix",
        "//src/core:posix_event_engine_wakeup_fd_posix_eventfd",
        "//src/core:posix_event_engine_wakeup_fd_posix_pipe",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "traced_buffer_list_test",
    srcs = ["traced_buffer_list_test.cc"],
    external_deps = [
        "absl/log:check",
        "gtest",
    ],
    tags = [
        "no_windows",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:gpr",
        "//src/core:iomgr_port",
        "//src/core:posix_event_engine_traced_buffer_list",
        "//src/core:time",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "posix_write_event_sink_test",
    srcs = ["posix_write_event_sink_test.cc"],
    external_deps = [
        "gtest",
    ],
    tags = [
        "no_windows",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//src/core:posix_write_event_sink",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "tcp_posix_socket_utils_test",
    srcs = ["tcp_posix_socket_utils_test.cc"],
    external_deps = [
        "gtest",
        "absl/status",
    ],
    tags = [
        "no_windows",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:gpr",
        "//:grpc",
        "//src/core:event_engine_common",
        "//src/core:iomgr_port",
        "//src/core:posix_event_engine_posix_interface",
        "//src/core:posix_event_engine_tcp_socket_utils",
        "//src/core:socket_mutator",
        "//src/core:useful",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "posix_endpoint_test",
    srcs = ["posix_endpoint_test.cc"],
    external_deps = [
        "absl/log:check",
        "absl/log:log",
        "absl/status:statusor",
        "absl/strings",
        "gtest",
    ],
    tags = [
        "no_windows",
    ],
    uses_event_engine = True,
    uses_polling = True,
    deps = [
        "//:config_vars",
        "//:event_engine_base_hdrs",
        "//:gpr",
        "//:grpc",
        "//:ref_counted_ptr",
        "//src/core:channel_args",
        "//src/core:channel_args_endpoint_config",
        "//src/core:common_event_engine_closures",
        "//src/core:dual_ref_counted",
        "//src/core:event_engine_poller",
        "//src/core:event_engine_tcp_socket_utils",
        "//src/core:notification",
        "//src/core:posix_event_engine",
        "//src/core:posix_event_engine_closure",
        "//src/core:posix_event_engine_endpoint",
        "//src/core:posix_event_engine_event_poller",
        "//src/core:posix_event_engine_poller_posix_default",
        "//src/core:posix_event_engine_tcp_socket_utils",
        "//src/core:resource_quota",
        "//src/core:wait_for_single_owner",
        "//test/core/event_engine:event_engine_test_utils",
        "//test/core/event_engine/posix:posix_engine_test_utils",
        "//test/core/event_engine/test_suite/posix:oracle_event_engine_posix",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "posix_engine_listener_utils_test",
    srcs = ["posix_engine_listener_utils_test.cc"],
    external_deps = [
        "absl/log:log",
        "gtest",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
    ],
    tags = [
        "no_windows",
    ],
    uses_event_engine = False,
    deps = [
        "//:event_engine_base_hdrs",
        "//src/core:channel_args_endpoint_config",
        "//src/core:event_engine_common",
        "//src/core:event_engine_tcp_socket_utils",
        "//src/core:iomgr_port",
        "//src/core:posix_event_engine_listener_utils",
        "//src/core:posix_event_engine_tcp_socket_utils",
        "//src/core:socket_mutator",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "posix_event_engine_connect_test",
    srcs = ["posix_event_engine_connect_test.cc"],
    external_deps = [
        "absl/log:check",
        "absl/log:log",
        "absl/memory",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
        "gtest",
    ],
    tags = [
        "no_mac",
        "no_windows",
    ],
    uses_event_engine = True,
    uses_polling = True,
    deps = [
        "//:event_engine_base_hdrs",
        "//:gpr",
        "//:grpc",
        "//src/core:channel_args",
        "//src/core:channel_args_endpoint_config",
        "//src/core:event_engine_poller",
        "//src/core:event_engine_tcp_socket_utils",
        "//src/core:memory_quota",
        "//src/core:notification",
        "//src/core:posix_event_engine",
        "//src/core:posix_event_engine_closure",
        "//src/core:posix_event_engine_endpoint",
        "//src/core:posix_event_engine_event_poller",
        "//src/core:posix_event_engine_poller_posix_default",
        "//src/core:resource_quota",
        "//src/core:wait_for_single_owner",
        "//test/core/event_engine:event_engine_test_utils",
        "//test/core/event_engine/test_suite/posix:oracle_event_engine_posix",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "log_too_many_open_files_test",
    srcs = ["log_too_many_open_files_test.cc"],
    external_deps = [
        "gtest",
        "absl/status",
        "absl/status:statusor",
    ],
    tags = [
        "no_windows",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:grpc",
        "//src/core:event_engine_tcp_socket_utils",
        "//src/core:posix_event_engine",
        "//src/core:posix_event_engine_posix_interface",
        "//src/core:strerror",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "file_descriptor_collection_test",
    srcs = ["file_descriptor_collection_test.cc"],
    external_deps = [
        "absl/log:check",
        "absl/log:log",
        "gtest",
    ],
    tags = [
        "no_windows",
    ],
    uses_event_engine = True,
    uses_polling = True,
    deps = [
        "//:grpc",
        "//src/core:experiments",
        "//src/core:posix_event_engine_file_descriptor_collection",
        "//src/core:posix_event_engine_posix_interface",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "poller_fork_test",
    srcs = ["poller_fork_test.cc"],
    external_deps = [
        "absl/functional:any_invocable",
        "absl/log:log",
        "absl/status",
        "absl/strings",
        "gtest",
    ],
    tags = [
        "no_windows",
    ],
    uses_event_engine = True,
    uses_polling = True,
    deps = [
        ":posix_engine_test_utils",
        "//:event_engine_base_hdrs",
        "//:grpc++",
        "//:grpc++_base",
        "//src/core:channel_args",
        "//src/core:channel_args_endpoint_config",
        "//src/core:event_engine_tcp_socket_utils",
        "//src/core:posix_event_engine",
        "//src/core:wait_for_single_owner",
        "//test/core/event_engine:event_engine_test_utils",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_library(
    name = "dns_server",
    testonly = True,
    srcs = ["dns_server.cc"],
    hdrs = ["dns_server.h"],
    external_deps = [
        "absl/status:statusor",
    ],
    tags = [
        "no_windows",
    ],
    visibility = ["//visibility:private"],
    deps = [
        "//:gpr_platform",
        "//:grpc++",
        "//src/core:notification",
    ],
)

grpc_cc_test(
    name = "dns_fork_test",
    srcs = ["dns_fork_test.cc"],
    external_deps = [
        "gtest",
        "absl/status",
        "absl/status:statusor",
    ],
    tags = [
        "no_windows",
    ],
    uses_event_engine = True,
    uses_polling = True,
    deps = [
        ":dns_server",
        "//:event_engine_base_hdrs",
        "//:gpr_platform",
        "//:grpc",
        "//src/core:event_engine_tcp_socket_utils",
        "//src/core:notification",
        "//src/core:posix_event_engine",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "errqueue_support_test",
    srcs = ["errqueue_support_test.cc"],
    external_deps = [
        "gtest",
    ],
    tags = [
        # Only relevant for linux
        "no_mac",
        "no_windows",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//src/core:posix_event_engine_internal_errqueue",
        "//test/core/test_util:grpc_test_util",
    ],
)
