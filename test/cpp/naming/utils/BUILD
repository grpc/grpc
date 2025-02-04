# Copyright 2017 gRPC authors.
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

load("//bazel:grpc_build_system.bzl", "grpc_py_binary")

package(
    default_visibility = ["//visibility:public"],
    features = [
        "-layering_check",
        "-parse_headers",
    ],
)

licenses(["notice"])

grpc_py_binary(
    name = "dns_server",
    testonly = True,
    srcs = ["dns_server.py"],
    external_deps = [
        "twisted",
        "yaml",
    ],
)

grpc_py_binary(
    name = "dns_resolver",
    testonly = True,
    srcs = ["dns_resolver.py"],
    external_deps = [
        "twisted",
    ],
)

grpc_py_binary(
    name = "tcp_connect",
    testonly = True,
    srcs = ["tcp_connect.py"],
)

grpc_py_binary(
    name = "health_check",
    testonly = True,
    srcs = ["health_check.py"],
)
