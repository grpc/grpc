# Copyright 2024 gRPC authors.
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

load("//fuzztest:grpc_fuzz_test.bzl", "grpc_fuzz_test")

grpc_fuzz_test(
    name = "auto_scaler_fuzztest",
    srcs = ["auto_scaler_fuzztest.cc"],
    external_deps = [
        "fuzztest",
        "fuzztest_main",
        "gtest",
    ],
    deps = ["//src/core:chaotic_good_auto_scaler"],
)

grpc_fuzz_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    external_deps = [
        "fuzztest",
        "fuzztest_main",
        "gtest",
    ],
    deps = [
        "//src/core:chaotic_good_config",
        "//src/core:chaotic_good_frame_cc_proto",
    ],
)

grpc_fuzz_test(
    name = "message_chunker_test",
    srcs = ["message_chunker_test.cc"],
    external_deps = [
        "fuzztest",
        "fuzztest_main",
        "gtest",
    ],
    deps = [
        "//src/core:chaotic_good_frame_cc_proto",
        "//src/core:chaotic_good_message_chunker",
        "//src/core:status_flag",
        "//test/core/promise:poll_matcher",
    ],
)
