# Copyright 2022 gRPC authors.
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

load(
    "//bazel:grpc_build_system.bzl",
    "grpc_cc_library",
    "grpc_cc_test",
    "grpc_package",
)

grpc_package(name = "test/core/client_channel/lb_policy")

licenses(["notice"])

grpc_cc_library(
    name = "lb_policy_test_lib",
    testonly = True,
    hdrs = ["lb_policy_test_lib.h"],
    external_deps = [
        "gtest",
    ],
    language = "C++",
    deps = [
        "//:lb_policy",
        "//:subchannel_interface",
    ],
)

grpc_cc_test(
    name = "pick_first_test",
    srcs = ["pick_first_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    deps = [
        ":lb_policy_test_lib",
        "//:grpc_lb_policy_pick_first",
        "//test/core/util:grpc_test_util",
    ],
)
