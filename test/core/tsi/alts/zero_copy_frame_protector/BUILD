# Copyright 2018 gRPC authors.
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

licenses(["notice"])  # Apache v2

grpc_package(name = "test/core/tsi/alts/zero_copy_frame_protector")

grpc_cc_test(
    name = "alts_grpc_record_protocol_test",
    srcs = ["alts_grpc_record_protocol_test.cc"],
    language = "C++",
    deps = [
        "//:alts_frame_protector",
        "//:gpr",
        "//:grpc",
        "//:grpc_base_c",
        "//test/core/tsi/alts/crypt:alts_crypt_test_util",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "alts_iovec_record_protocol_test",
    srcs = ["alts_iovec_record_protocol_test.cc"],
    language = "C++",
    deps = [
        "//:alts_frame_protector",
        "//:gpr",
        "//:grpc",
        "//test/core/tsi/alts/crypt:alts_crypt_test_util",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "alts_zero_copy_grpc_protector_test",
    srcs = ["alts_zero_copy_grpc_protector_test.cc"],
    language = "C++",
    deps = [
        "//:alts_frame_protector",
        "//:gpr",
        "//:grpc",
        "//:grpc_base_c",
        "//test/core/tsi/alts/crypt:alts_crypt_test_util",
        "//test/core/util:grpc_test_util",
    ],
)
