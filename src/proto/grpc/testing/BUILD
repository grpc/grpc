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

load("@rules_proto//proto:defs.bzl", "proto_library")
load("//bazel:grpc_build_system.bzl", "grpc_package", "grpc_proto_library")
load("//bazel:python_rules.bzl", "py_grpc_library", "py_proto_library")

licenses(["notice"])

grpc_package(
    name = "testing",
    visibility = "public",
)

exports_files([
    "echo.proto",
    "echo_messages.proto",
    "empty.proto",
    "messages.proto",
    "simple_messages.proto",
    "test.proto",
])

grpc_proto_library(
    name = "compiler_test_proto",
    srcs = ["compiler_test.proto"],
    generate_mocks = True,
)

grpc_proto_library(
    name = "control_proto",
    srcs = ["control.proto"],
    has_services = False,
    well_known_protos = True,
    deps = [
        "payloads_proto",
        "stats_proto",
    ],
)

grpc_proto_library(
    name = "echo_messages_proto",
    srcs = ["echo_messages.proto"],
    deps = [
        "//src/proto/grpc/testing/xds/v3:orca_load_report_proto",
    ],
)

grpc_proto_library(
    name = "echo_proto",
    srcs = ["echo.proto"],
    generate_mocks = True,
    deps = [
        "echo_messages_proto",
        "simple_messages_proto",
    ],
)

grpc_proto_library(
    name = "empty_proto",
    srcs = ["empty.proto"],
    has_services = False,
)

proto_library(
    name = "empty_proto_descriptor",
    srcs = ["empty.proto"],
)

py_proto_library(
    name = "empty_py_pb2",
    deps = [":empty_proto_descriptor"],
)

py_grpc_library(
    name = "empty_py_pb2_grpc",
    srcs = [":empty_proto_descriptor"],
    deps = [":empty_py_pb2"],
)

grpc_proto_library(
    name = "messages_proto",
    srcs = ["messages.proto"],
    has_services = False,
)

proto_library(
    name = "messages_proto_descriptor",
    srcs = ["messages.proto"],
)

py_proto_library(
    name = "py_messages_proto",
    deps = [":messages_proto_descriptor"],
)

py_grpc_library(
    name = "messages_py_pb2_grpc",
    srcs = [":messages_proto_descriptor"],
    deps = [":py_messages_proto"],
)

grpc_proto_library(
    name = "metrics_proto",
    srcs = ["metrics.proto"],
)

grpc_proto_library(
    name = "payloads_proto",
    srcs = ["payloads.proto"],
    has_services = False,
)

grpc_proto_library(
    name = "benchmark_service_proto",
    srcs = ["benchmark_service.proto"],
    deps = [
        "messages_proto",
    ],
)

proto_library(
    name = "benchmark_service_descriptor",
    srcs = ["benchmark_service.proto"],
    deps = [":messages_proto_descriptor"],
)

py_proto_library(
    name = "benchmark_service_py_pb2",
    deps = [":benchmark_service_descriptor"],
)

py_grpc_library(
    name = "benchmark_service_py_pb2_grpc",
    srcs = [":benchmark_service_descriptor"],
    deps = [":benchmark_service_py_pb2"],
)

grpc_proto_library(
    name = "report_qps_scenario_service_proto",
    srcs = ["report_qps_scenario_service.proto"],
    deps = [
        "control_proto",
    ],
)

grpc_proto_library(
    name = "worker_service_proto",
    srcs = ["worker_service.proto"],
    deps = [
        "control_proto",
    ],
)

grpc_proto_library(
    name = "simple_messages_proto",
    srcs = ["simple_messages.proto"],
)

grpc_proto_library(
    name = "stats_proto",
    srcs = ["stats.proto"],
    has_services = False,
)

grpc_proto_library(
    name = "test_proto",
    srcs = ["test.proto"],
    deps = [
        "empty_proto",
        "messages_proto",
    ],
)

# Test that grpc_proto_library/cc_grpc_library can consume generated files
genrule(
    name = "messages_gen_proto_file",
    srcs = ["messages.proto"],
    outs = ["messages_gen.proto"],
    cmd = "cp $< $@",
)

grpc_proto_library(
    name = "messages_gen_proto",
    srcs = ["messages_gen_proto_file"],
    has_services = False,
)

genrule(
    name = "test_gen_proto_file",
    srcs = ["test.proto"],
    outs = ["test_gen.proto"],
    cmd = "sed 's/messages.proto/messages_gen.proto/' $< > $@",
)

# Consume generated files in srcs and in deps
grpc_proto_library(
    name = "test_gen_proto",
    srcs = ["test_gen_proto_file"],
    deps = [
        "empty_proto",
        "messages_gen_proto",
    ],
)

proto_library(
    name = "test_proto_descriptor",
    srcs = ["test.proto"],
    deps = [
        ":empty_proto_descriptor",
        ":messages_proto_descriptor",
    ],
)

py_proto_library(
    name = "py_test_proto",
    deps = [":test_proto_descriptor"],
)

py_grpc_library(
    name = "test_py_pb2_grpc",
    srcs = [":test_proto_descriptor"],
    deps = [":py_test_proto"],
)

proto_library(
    name = "worker_service_descriptor",
    srcs = ["worker_service.proto"],
    deps = [":control_descriptor"],
)

py_proto_library(
    name = "worker_service_py_pb2",
    deps = [":worker_service_descriptor"],
)

py_grpc_library(
    name = "worker_service_py_pb2_grpc",
    srcs = [":worker_service_descriptor"],
    deps = [":worker_service_py_pb2"],
)

proto_library(
    name = "stats_descriptor",
    srcs = ["stats.proto"],
)

py_proto_library(
    name = "stats_py_pb2",
    deps = [":stats_descriptor"],
)

proto_library(
    name = "payloads_descriptor",
    srcs = ["payloads.proto"],
)

py_proto_library(
    name = "payloads_py_pb2",
    deps = [":payloads_descriptor"],
)

proto_library(
    name = "control_descriptor",
    srcs = ["control.proto"],
    deps = [
        ":payloads_descriptor",
        ":stats_descriptor",
        "@com_google_protobuf//:timestamp_proto",
    ],
)

py_proto_library(
    name = "control_py_pb2",
    deps = [":control_descriptor"],
)

grpc_proto_library(
    name = "istio_echo_proto",
    srcs = ["istio_echo.proto"],
    well_known_protos = True,
)
