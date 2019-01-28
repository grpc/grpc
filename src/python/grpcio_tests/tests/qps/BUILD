# Copyright 2019 The gRPC Authors
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

package(default_visibility = ["//visibility:public"])

load("@grpc_python_dependencies//:requirements.bzl", "requirement")

py_library(
    name = "benchmark_client",
    srcs = ["benchmark_client.py"],
    imports = ["../../"],
    deps = [
        requirement("six"),
        "//src/proto/grpc/testing:py_benchmark_service_proto",
        "//src/proto/grpc/testing:py_messages_proto",
        "//src/python/grpcio/grpc:grpcio",
        "//src/python/grpcio_tests/tests/unit:resources",
        "//src/python/grpcio_tests/tests/unit:test_common",
    ],
)

py_library(
    name = "benchmark_server",
    srcs = ["benchmark_server.py"],
    imports = ["../../"],
    deps = [
        "//src/proto/grpc/testing:py_benchmark_service_proto",
        "//src/proto/grpc/testing:py_messages_proto",
    ],
)

py_library(
    name = "client_runner",
    srcs = ["client_runner.py"],
    imports = ["../../"],
)

py_library(
    name = "histogram",
    srcs = ["histogram.py"],
    imports = ["../../"],
    deps = [
        "//src/proto/grpc/testing:py_stats_proto",
    ],
)

py_library(
    name = "worker_server",
    srcs = ["worker_server.py"],
    imports = ["../../"],
    deps = [
        ":benchmark_client",
        ":benchmark_server",
        ":client_runner",
        ":histogram",
        "//src/proto/grpc/testing:py_benchmark_service_proto",
        "//src/proto/grpc/testing:py_control_proto",
        "//src/proto/grpc/testing:py_stats_proto",
        "//src/proto/grpc/testing:py_worker_service_proto",
        "//src/python/grpcio/grpc:grpcio",
        "//src/python/grpcio_tests/tests/unit:resources",
        "//src/python/grpcio_tests/tests/unit:test_common",
    ],
)

py_binary(
    name = "qps_worker",
    srcs = ["qps_worker.py"],
    imports = ["../../"],
    main = "qps_worker.py",
    deps = [
        ":worker_server",
        "//src/proto/grpc/testing:py_worker_service_proto",
        "//src/python/grpcio/grpc:grpcio",
        "//src/python/grpcio_tests/tests/unit:test_common",
    ],
)

filegroup(
    name = "scenarios",
    srcs = ["scenarios.json"],
)

sh_test(
    name = "basic_benchmark_test",
    srcs = ["basic_benchmark_test.sh"],
    data = [
        ":qps_worker",
        ":scenarios",
        "//test/cpp/qps:qps_json_driver",
    ],
)
