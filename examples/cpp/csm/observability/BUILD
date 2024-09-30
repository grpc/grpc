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

cc_binary(
    name = "csm_greeter_client",
    srcs = ["csm_greeter_client.cc"],
    defines = ["BAZEL_BUILD"],
    deps = [
        "//:grpc++",
        "//:grpcpp_csm_observability",
        "//examples/cpp/otel:util",
        "//examples/protos:helloworld_cc_grpc",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/flags:parse",
        "@io_opentelemetry_cpp//exporters/prometheus:prometheus_exporter",
        "@io_opentelemetry_cpp//sdk/src/metrics",
    ],
)

cc_binary(
    name = "csm_greeter_server",
    srcs = ["csm_greeter_server.cc"],
    defines = ["BAZEL_BUILD"],
    deps = [
        "//:grpc++",
        "//:grpc++_reflection",
        "//:grpcpp_admin",
        "//:grpcpp_csm_observability",
        "//examples/cpp/otel:util",
        "//examples/protos:helloworld_cc_grpc",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/flags:parse",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/log:globals",
        "@com_google_absl//absl/log:initialize",
        "@io_opentelemetry_cpp//exporters/prometheus:prometheus_exporter",
        "@io_opentelemetry_cpp//sdk/src/metrics",
    ],
)
