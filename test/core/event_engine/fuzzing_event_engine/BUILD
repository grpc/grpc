# Copyright 2021 gRPC authors.
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

load("//bazel:grpc_build_system.bzl", "grpc_cc_library", "grpc_package", "grpc_proto_library")

licenses(["notice"])

grpc_package(
    name = "test/core/event_engine/fuzzing_event_engine",
    visibility = "tests",
)

grpc_cc_library(
    name = "fuzzing_event_engine",
    srcs = ["fuzzing_event_engine.cc"],
    hdrs = ["fuzzing_event_engine.h"],
    deps = [
        ":fuzzing_event_engine_proto",
        "//:event_engine_base_hdrs",
        "//src/core:default_event_engine",
        "//src/core:time",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_proto_library(
    name = "fuzzing_event_engine_proto",
    srcs = ["fuzzing_event_engine.proto"],
    has_services = False,
)
