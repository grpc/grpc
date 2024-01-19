# Copyright 2020 the gRPC authors.
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

licenses(["notice"])

cc_binary(
    name = "greeter_client",
    srcs = ["greeter_client.cc"],
    defines = ["BAZEL_BUILD"],
    deps = [
        "//:grpc++",
        "//examples/protos:helloworld_cc_grpc",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/flags:parse",
    ],
)

cc_binary(
    name = "greeter_async_client",
    srcs = ["greeter_async_client.cc"],
    defines = ["BAZEL_BUILD"],
    deps = [
        "//:grpc++",
        "//examples/protos:helloworld_cc_grpc",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/flags:parse",
    ],
)

cc_binary(
    name = "greeter_async_client2",
    srcs = ["greeter_async_client2.cc"],
    defines = ["BAZEL_BUILD"],
    deps = [
        "//:grpc++",
        "//examples/protos:helloworld_cc_grpc",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/flags:parse",
    ],
)

cc_binary(
    name = "greeter_callback_client",
    srcs = ["greeter_callback_client.cc"],
    defines = ["BAZEL_BUILD"],
    deps = [
        "//:grpc++",
        "//examples/protos:helloworld_cc_grpc",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/flags:parse",
    ],
)

cc_binary(
    name = "xds_greeter_client",
    srcs = ["xds_greeter_client.cc"],
    defines = ["BAZEL_BUILD"],
    deps = [
        "//:grpc++",
        "//examples/protos:helloworld_cc_grpc",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/flags:parse",
    ],
)

cc_binary(
    name = "greeter_server",
    srcs = ["greeter_server.cc"],
    defines = ["BAZEL_BUILD"],
    deps = [
        "//:grpc++",
        "//:grpc++_reflection",
        "//examples/protos:helloworld_cc_grpc",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/flags:parse",
        "@com_google_absl//absl/strings:str_format",
    ],
)

cc_binary(
    name = "greeter_async_server",
    srcs = ["greeter_async_server.cc"],
    defines = ["BAZEL_BUILD"],
    deps = [
        "//:grpc++",
        "//examples/protos:helloworld_cc_grpc",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/flags:parse",
        "@com_google_absl//absl/strings:str_format",
    ],
)

cc_binary(
    name = "greeter_callback_server",
    srcs = ["greeter_callback_server.cc"],
    defines = ["BAZEL_BUILD"],
    deps = [
        "//:grpc++",
        "//:grpc++_reflection",
        "//examples/protos:helloworld_cc_grpc",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/flags:parse",
        "@com_google_absl//absl/strings:str_format",
    ],
)

cc_binary(
    name = "xds_greeter_server",
    srcs = ["xds_greeter_server.cc"],
    defines = ["BAZEL_BUILD"],
    deps = [
        "//:grpc++",
        "//:grpc++_reflection",
        "//:grpcpp_admin",
        "//examples/protos:helloworld_cc_grpc",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/flags:parse",
    ],
)
