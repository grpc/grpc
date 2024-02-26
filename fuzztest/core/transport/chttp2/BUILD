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

load("//fuzztest:grpc_fuzz_test.bzl", "grpc_fuzz_test")

grpc_fuzz_test(
    name = "write_size_policy_test",
    srcs = ["write_size_policy_test.cc"],
    external_deps = [
        "fuzztest",
        "fuzztest_main",
        "gtest",
    ],
    deps = ["//src/core:write_size_policy"],
)

grpc_fuzz_test(
    name = "hpack_encoder_timeout_test",
    srcs = ["hpack_encoder_timeout_test.cc"],
    external_deps = [
        "absl/random",
        "fuzztest",
        "fuzztest_main",
        "gtest",
    ],
    deps = [
        "//:grpc_base",
        "//:hpack_encoder",
        "//:hpack_parser",
        "//:ref_counted_ptr",
        "//src/core:arena",
        "//src/core:memory_quota",
        "//src/core:metadata_batch",
        "//src/core:resource_quota",
        "//src/core:slice_buffer",
        "//src/core:time",
    ],
)
