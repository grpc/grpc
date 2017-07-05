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

licenses(["notice"])  # Apache v2

cc_library(
    name = "server_helper_lib",
    srcs = [
        "server_helper.cc",
    ],
    hdrs = [
        "server_helper.h",
    ],
    deps = [
        "//test/cpp/util:test_util",
        "//external:gflags",
    ],
)

cc_binary(
    name = "interop_server",
    srcs = [
        "interop_server.cc",
        "interop_server_bootstrap.cc",
    ],
    deps = [
        ":server_helper_lib",
        "//:grpc++",
        "//src/proto/grpc/testing:empty_proto",
        "//src/proto/grpc/testing:messages_proto",
        "//src/proto/grpc/testing:test_proto",
        "//test/cpp/util:test_config",
    ],
)

cc_library(
    name = "client_helper_lib",
    srcs = [
        "client_helper.cc",
        "interop_client.cc",
    ],
    hdrs = [
        "client_helper.h",
        "interop_client.h",
    ],
    deps = [
        "//test/cpp/util:test_util",
        "//src/proto/grpc/testing:empty_proto",
        "//src/proto/grpc/testing:messages_proto",
        "//src/proto/grpc/testing:test_proto",
        "//test/core/security:oauth2_utils",
        "//test/cpp/util:test_config",
    ],
)

cc_binary(
    name = "interop_client",
    srcs = [
        "client.cc",
    ],
    deps = [
        ":client_helper_lib",
    ],
)
