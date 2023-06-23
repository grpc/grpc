# Copyright 2023 gRPC authors.
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

load("//bazel:grpc_build_system.bzl", "grpc_cc_library", "grpc_cc_test")

grpc_cc_library(
    name = "experiments_lib",
    srcs = [
        "fixtures/experiments.cc",
    ],
    hdrs = [
        "fixtures/experiments.h",
    ],
    language = "c++",
    deps = [
        "//:config_vars",
        "//:gpr",
        "//src/core:experiments",
        "//src/core:no_destruct",
    ],
)

grpc_cc_test(
    name = "experiments_test",
    srcs = ["experiments_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        ":experiments_lib",
        "//test/core/util:grpc_test_util",
    ],
)
