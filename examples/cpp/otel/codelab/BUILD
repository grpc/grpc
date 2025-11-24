# Copyright 2023 the gRPC authors.
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

package(
    default_visibility = ["//examples/cpp:__subpackages__"],
)

cc_library(
    name = "util",
    srcs = ["util.cc"],
    hdrs = ["util.h"],
    defines = ["BAZEL_BUILD"],
    deps = [
        "//:grpc++",
        "//examples/protos:helloworld_cc_grpc",
        "@io_opentelemetry_cpp//sdk/src/metrics",
    ],
)

cc_binary(
    name = "greeter_callback_client",
    srcs = ["greeter_callback_client.cc"],
    defines = ["BAZEL_BUILD"],
    deps = [
        "util",
        "//:grpc++",
        "//:grpcpp_otel_plugin",
        "//examples/cpp/otel:util",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/flags:parse",
        "@com_google_absl//absl/log:initialize",
        "@io_opentelemetry_cpp//exporters/prometheus:prometheus_exporter",
        "@io_opentelemetry_cpp//sdk/src/metrics",
    ],
)

cc_binary(
    name = "greeter_callback_server",
    srcs = ["greeter_callback_server.cc"],
    defines = ["BAZEL_BUILD"],
    deps = [
        "util",
        "//:grpc++",
        "//:grpc++_reflection",
        "//:grpcpp_otel_plugin",
        "//examples/cpp/otel:util",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/flags:parse",
        "@com_google_absl//absl/log:initialize",
        "@com_google_absl//absl/strings:str_format",
        "@io_opentelemetry_cpp//exporters/prometheus:prometheus_exporter",
        "@io_opentelemetry_cpp//sdk/src/metrics",
    ],
)

cc_binary(
    name = "greeter_callback_client_solution",
    srcs = ["greeter_callback_client_solution.cc"],
    defines = ["BAZEL_BUILD"],
    deps = [
        "util",
        "//:grpc++",
        "//:grpcpp_otel_plugin",
        "//examples/cpp/otel:util",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/flags:parse",
        "@com_google_absl//absl/log:initialize",
        "@io_opentelemetry_cpp//exporters/prometheus:prometheus_exporter",
        "@io_opentelemetry_cpp//sdk/src/metrics",
    ],
)

cc_binary(
    name = "greeter_callback_server_solution",
    srcs = ["greeter_callback_server_solution.cc"],
    defines = ["BAZEL_BUILD"],
    deps = [
        "util",
        "//:grpc++",
        "//:grpc++_reflection",
        "//:grpcpp_otel_plugin",
        "//examples/cpp/otel:util",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/flags:parse",
        "@com_google_absl//absl/log:initialize",
        "@com_google_absl//absl/strings:str_format",
        "@io_opentelemetry_cpp//exporters/prometheus:prometheus_exporter",
        "@io_opentelemetry_cpp//sdk/src/metrics",
    ],
)
