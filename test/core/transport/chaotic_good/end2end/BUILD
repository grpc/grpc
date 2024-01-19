# Copyright 2021 gRPC authors.
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
    name = "test/core/transport/chaotic_good/end2end",
    visibility = "tests",
)

grpc_cc_test(
    name = "chaotic_good_server_test",
    srcs = ["chaotic_good_server_test.cc"],
    external_deps = [
        "absl/functional:any_invocable",
        "absl/status",
        "absl/status:statusor",
        "absl/strings:str_format",
        "absl/types:optional",
        "gtest",
    ],
    language = "C++",
    tags = [
        "no_windows",
    ],
    uses_event_engine = True,
    uses_polling = False,
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//:grpc_public_hdrs",
        "//:iomgr_timer",
        "//src/core:channel_args",
        "//src/core:chaotic_good_connector",
        "//src/core:chaotic_good_server",
        "//src/core:resource_quota",
        "//test/core/util:grpc_test_util",
    ],
)
