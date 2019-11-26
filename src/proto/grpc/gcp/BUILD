# Copyright 2019 gRPC authors.
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

load("//bazel:grpc_build_system.bzl", "grpc_package", "grpc_proto_library")

grpc_package(
    name = "src/proto/grpc/gcp",
    visibility = "public",
)

# It is a known issue that for some Linux distributions built using musl, the
# names(major and minor, more specifically) defined in
# transport_security_common.proto has a conflict with system macro defined in
# sysmacros.h. Thus if your system uses musl and depends on this library, it
# MAY have problems.
# Future reference:
# https://www.openwall.com/lists/musl/2019/06/14/4 and
# discussions in PR: https://github.com/grpc/grpc/pull/21196
grpc_proto_library(
    name = "transport_security_common_proto",
    srcs = ["transport_security_common.proto"],
    has_services = False,
)

grpc_proto_library(
    name = "altscontext_proto",
    srcs = ["altscontext.proto"],
    has_services = False,
    deps = [
        "transport_security_common_proto",
    ],
)
