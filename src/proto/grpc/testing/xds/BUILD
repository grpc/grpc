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

load("//bazel:grpc_build_system.bzl", "grpc_package", "grpc_proto_library")

grpc_package(
    name = "xds",
    visibility = "public",
)

grpc_proto_library(
    name = "ads_for_test_proto",
    srcs = [
        "ads_for_test.proto",
    ],
    has_services = True,
    deps = ["eds_for_test_proto"],
)

grpc_proto_library(
    name = "cds_for_test_proto",
    srcs = [
        "cds_for_test.proto",
    ],
    well_known_protos = True,
)

grpc_proto_library(
    name = "eds_for_test_proto",
    srcs = [
        "eds_for_test.proto",
    ],
    has_services = True,
    well_known_protos = True,
)

grpc_proto_library(
    name = "lds_rds_for_test_proto",
    srcs = [
        "lds_rds_for_test.proto",
    ],
    has_services = True,
    well_known_protos = True,
    deps = [
        "cds_for_test_proto",
        "eds_for_test_proto",
    ],
)

grpc_proto_library(
    name = "lrs_for_test_proto",
    srcs = [
        "lrs_for_test.proto",
    ],
    has_services = True,
    well_known_protos = True,
    deps = ["eds_for_test_proto"],
)

grpc_proto_library(
    name = "orca_load_report_for_test_proto",
    srcs = [
        "orca_load_report_for_test.proto",
    ],
)
