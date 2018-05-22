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

licenses(["notice"])  # Apache v2

load("//bazel:grpc_build_system.bzl", "grpc_proto_library", "grpc_cc_library", "grpc_cc_binary", "grpc_package")

grpc_package(name = "test/core/tsi/alts/fake_handshaker", visibility = "public")

grpc_proto_library(
    name = "transport_security_common_proto",
    srcs = ["transport_security_common.proto"],
    has_services = False,
)

grpc_proto_library(
    name = "handshaker_proto",
    srcs = ["handshaker.proto"],
    has_services = True,
    deps = [
        "transport_security_common_proto",
    ],
)

grpc_cc_library(
    name = "fake_handshaker_lib",
    testonly = True,
    srcs = ["fake_handshaker_server.cc"],
    language = "C++",
    deps = [
        "handshaker_proto",
        "transport_security_common_proto",
        "//:grpc++",
        "//test/cpp/util:test_config",
    ],
)

grpc_cc_binary(
    name = "fake_handshaker_server",
    testonly = True,
    srcs = ["fake_handshaker_server.cc"],
    language = "C++",
    deps = [
        "fake_handshaker_lib",
    ],
)
