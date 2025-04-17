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

load(
    "//bazel:grpc_build_system.bzl",
    "grpc_cc_grpc_library",
    "grpc_cc_proto_library",
    "grpc_internal_proto_library",
    "grpc_package",
    "grpc_proto_library",
)
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

grpc_internal_proto_library(
    name = "compiler_test_proto",
    srcs = ["compiler_test.proto"],
    has_services = True,
)

grpc_cc_proto_library(
    name = "compiler_test_cc_proto",
    deps = ["compiler_test_proto"],
)

grpc_cc_grpc_library(
    name = "compiler_test_cc_grpc",
    srcs = ["compiler_test_proto"],
    allow_deprecated = True,
    generate_mocks = True,
    deps = ["compiler_test_cc_proto"],
)

grpc_internal_proto_library(
    name = "control_proto",
    srcs = ["control.proto"],
    deps = [
        "payloads_proto",
        "stats_proto",
        "@com_google_protobuf//:timestamp_proto",
    ],
)

grpc_cc_proto_library(
    name = "control_cc_proto",
    deps = ["control_proto"],
)

py_proto_library(
    name = "control_py_pb2",
    deps = [":control_proto"],
)

grpc_internal_proto_library(
    name = "echo_messages_proto",
    srcs = ["echo_messages.proto"],
    deps = [
        "@com_github_cncf_xds//xds/data/orca/v3:pkg",
    ],
)

grpc_cc_proto_library(
    name = "echo_messages_cc_proto",
    deps = ["echo_messages_proto"],
)

grpc_internal_proto_library(
    name = "echo_proto",
    srcs = ["echo.proto"],
    has_services = True,
    deps = [
        "echo_messages_proto",
        "simple_messages_proto",
    ],
)

grpc_cc_proto_library(
    name = "echo_cc_proto",
    deps = ["echo_proto"],
)

grpc_cc_grpc_library(
    name = "echo_cc_grpc",
    srcs = ["echo_proto"],
    generate_mocks = True,
    deps = [
        "echo_cc_proto",
        "echo_messages_cc_proto",
        "simple_messages_cc_proto",
    ],
)

grpc_internal_proto_library(
    name = "empty_proto",
    srcs = ["empty.proto"],
)

grpc_cc_proto_library(
    name = "empty_cc_proto",
    deps = ["empty_proto"],
)

py_proto_library(
    name = "empty_py_pb2",
    deps = [":empty_proto"],
)

py_grpc_library(
    name = "empty_py_pb2_grpc",
    srcs = [":empty_proto"],
    deps = [":empty_py_pb2"],
)

grpc_internal_proto_library(
    name = "messages_proto",
    srcs = ["messages.proto"],
)

grpc_cc_proto_library(
    name = "messages_cc_proto",
    deps = ["messages_proto"],
)

py_proto_library(
    name = "py_messages_proto",
    deps = [":messages_proto"],
)

py_grpc_library(
    name = "messages_py_pb2_grpc",
    srcs = [":messages_proto"],
    deps = [":py_messages_proto"],
)

grpc_internal_proto_library(
    name = "metrics_proto",
    srcs = ["metrics.proto"],
    has_services = True,
)

grpc_cc_proto_library(
    name = "metrics_cc_proto",
    deps = ["metrics_proto"],
)

grpc_cc_grpc_library(
    name = "metrics_cc_grpc",
    srcs = ["metrics_proto"],
    deps = ["metrics_cc_proto"],
)

grpc_internal_proto_library(
    name = "payloads_proto",
    srcs = ["payloads.proto"],
)

grpc_cc_proto_library(
    name = "payloads_cc_proto",
    deps = ["payloads_proto"],
)

py_proto_library(
    name = "payloads_py_pb2",
    deps = [":payloads_proto"],
)

grpc_internal_proto_library(
    name = "benchmark_service_proto",
    srcs = ["benchmark_service.proto"],
    has_services = True,
    deps = [
        "messages_proto",
    ],
)

grpc_cc_proto_library(
    name = "benchmark_service_cc_proto",
    deps = ["benchmark_service_proto"],
)

grpc_cc_grpc_library(
    name = "benchmark_service_cc_grpc",
    srcs = ["benchmark_service_proto"],
    deps = [
        "benchmark_service_cc_proto",
        "messages_cc_proto",
    ],
)

py_proto_library(
    name = "benchmark_service_py_pb2",
    deps = [":benchmark_service_proto"],
)

py_grpc_library(
    name = "benchmark_service_py_pb2_grpc",
    srcs = [":benchmark_service_proto"],
    deps = [":benchmark_service_py_pb2"],
)

grpc_internal_proto_library(
    name = "report_qps_scenario_service_proto",
    srcs = ["report_qps_scenario_service.proto"],
    has_services = True,
    deps = [
        "control_proto",
    ],
)

grpc_cc_proto_library(
    name = "report_qps_scenario_service_cc_proto",
    deps = ["report_qps_scenario_service_proto"],
)

grpc_cc_grpc_library(
    name = "report_qps_scenario_service_cc_grpc",
    srcs = ["report_qps_scenario_service_proto"],
    deps = [
        "control_cc_proto",
        "report_qps_scenario_service_cc_proto",
    ],
)

grpc_internal_proto_library(
    name = "worker_service_proto",
    srcs = ["worker_service.proto"],
    has_services = True,
    deps = [
        "control_proto",
    ],
)

grpc_cc_proto_library(
    name = "worker_service_cc_proto",
    deps = ["worker_service_proto"],
)

grpc_cc_grpc_library(
    name = "worker_service_cc_grpc",
    srcs = ["worker_service_proto"],
    deps = [
        "control_cc_proto",
        "worker_service_cc_proto",
    ],
)

py_proto_library(
    name = "worker_service_py_pb2",
    deps = [":worker_service_proto"],
)

py_grpc_library(
    name = "worker_service_py_pb2_grpc",
    srcs = [":worker_service_proto"],
    deps = [":worker_service_py_pb2"],
)

grpc_internal_proto_library(
    name = "simple_messages_proto",
    srcs = ["simple_messages.proto"],
)

grpc_cc_proto_library(
    name = "simple_messages_cc_proto",
    deps = [":simple_messages_proto"],
)

grpc_internal_proto_library(
    name = "stats_proto",
    srcs = ["stats.proto"],
)

grpc_cc_proto_library(
    name = "stats_cc_proto",
    deps = ["stats_proto"],
)

py_proto_library(
    name = "stats_py_pb2",
    deps = [":stats_proto"],
)

grpc_internal_proto_library(
    name = "test_proto",
    srcs = ["test.proto"],
    has_services = True,
    deps = [
        "empty_proto",
        "messages_proto",
    ],
)

grpc_cc_proto_library(
    name = "test_cc_proto",
    deps = ["test_proto"],
)

grpc_cc_grpc_library(
    name = "test_cc_grpc",
    srcs = ["test_proto"],
    deps = ["test_cc_proto"],
)

py_proto_library(
    name = "py_test_proto",
    deps = [":test_proto"],
)

py_grpc_library(
    name = "test_py_pb2_grpc",
    srcs = [":test_proto"],
    deps = [":py_test_proto"],
)

grpc_internal_proto_library(
    name = "istio_echo_proto",
    srcs = ["istio_echo.proto"],
    has_services = True,
    deps = [
        "@com_google_protobuf//:wrappers_proto",
    ],
)

grpc_cc_proto_library(
    name = "istio_echo_cc_proto",
    deps = ["istio_echo_proto"],
)

grpc_cc_grpc_library(
    name = "istio_echo_cc_grpc",
    srcs = ["istio_echo_proto"],
    deps = ["istio_echo_cc_proto"],
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

# The test_gen_proto rule below expects the name that grpc_proto_library
# would have generated for the dependency, but the dependency was actually
# generated by a separate proto_library rule, so we use an alias to make
# things work.
alias(
    name = "empty_proto_only",
    actual = "empty_proto",
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
