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

licenses(["notice"])

grpc_package(
    name = "test/core/event_engine/iomgr_event_engine",
    visibility = "public",
)  # Useful for third party devs to test their io manager implementation.

grpc_cc_test(
    name = "timer_heap_test",
    srcs = ["timer_heap_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:bitset",
        "//:iomgr_ee_timer",
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
        "//:iomgr_ee_timer",
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
        "//:iomgr_ee_closure",
        "//:iomgr_ee_event_poller",
        "//:iomgr_ee_poller_posix_default",
        "//:iomgr_event_engine",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "lock_free_event_test",
    srcs = ["lock_free_event_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    uses_event_engine = True,
    uses_polling = False,
    deps = [
        "//:iomgr_ee_closure",
        "//:iomgr_ee_event_poller",
        "//:iomgr_ee_lockfree_event",
        "//:iomgr_event_engine",
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
        "//:iomgr_ee_wakeup_fd_posix_eventfd",
        "//:iomgr_ee_wakeup_fd_posix_pipe",
        "//test/core/util:grpc_test_util",
    ],
)
