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
    name = "greeter_callback_client",
    srcs = ["greeter_callback_client.cc"],
    defines = ["BAZEL_BUILD"],
    deps = [
        "//:grpcpp_otel_plugin",
        "//examples/cpp/otel:util",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/flags:parse",
        "@io_opentelemetry_cpp//exporters/ostream:ostream_metric_exporter",
        "@io_opentelemetry_cpp//exporters/ostream:ostream_span_exporter",
        "@io_opentelemetry_cpp//sdk/src/metrics",
        "@io_opentelemetry_cpp//sdk/src/trace",
    ],
)

cc_binary(
    name = "greeter_callback_server",
    srcs = ["greeter_callback_server.cc"],
    defines = ["BAZEL_BUILD"],
    deps = [
        "//:grpcpp_otel_plugin",
        "//examples/cpp/otel:util",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/flags:parse",
        "@com_google_absl//absl/strings:str_format",
        "@io_opentelemetry_cpp//exporters/ostream:ostream_metric_exporter",
        "@io_opentelemetry_cpp//exporters/ostream:ostream_span_exporter",
        "@io_opentelemetry_cpp//sdk/src/metrics",
        "@io_opentelemetry_cpp//sdk/src/trace",
    ],
)
