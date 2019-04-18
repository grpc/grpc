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

load("//bazel:grpc_build_system.bzl", "grpc_cc_library", "grpc_cc_test", "grpc_package")

licenses(["notice"])  # Apache v2

grpc_package(name = "test/core/tsi/alts/frame_protector")

grpc_cc_test(
    name = "alts_counter_test",
    srcs = ["alts_counter_test.cc"],
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
    name = "alts_crypter_test",
    srcs = ["alts_crypter_test.cc"],
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
    name = "alts_frame_protector_test",
    srcs = ["alts_frame_protector_test.cc"],
    language = "C++",
    deps = [
        "//:alts_frame_protector",
        "//:gpr",
        "//:grpc",
        "//:tsi",
        "//:tsi_interface",
        "//test/core/tsi:transport_security_test_lib",
        "//test/core/tsi/alts/crypt:alts_crypt_test_util",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "frame_handler_test",
    srcs = ["frame_handler_test.cc"],
    language = "C++",
    deps = [
        "//:alts_frame_protector",
        "//:gpr",
        "//:gpr_base",
        "//:grpc",
        "//test/core/tsi/alts/crypt:alts_crypt_test_util",
        "//test/core/util:grpc_test_util",
    ],
)
