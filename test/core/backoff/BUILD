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

load("//bazel:grpc_build_system.bzl", "grpc_cc_test")

licenses(["notice"])

package(
    features = [
        "-layering_check",
        "-parse_headers",
    ],
)

grpc_cc_test(
    name = "backoff_test",
    srcs = ["backoff_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:backoff",
        "//:exec_ctx",
        "//:grpc",
        "//src/core:time",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "random_early_detection_test",
    srcs = ["random_early_detection_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    uses_event_engine = False,
    uses_polling = False,
    deps = ["//src/core:random_early_detection"],
)
