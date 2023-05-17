# Copyright 2023 gRPC authors.
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

load("//bazel:grpc_build_system.bzl", "grpc_package")
load("//test/core/util:grpc_fuzzer.bzl", "grpc_proto_fuzzer")

grpc_package(name = "test/core/ext/filters/event_engine_client_channel_resolver")

licenses(["notice"])

grpc_proto_fuzzer(
    name = "resolver_fuzzer",
    size = "enormous",
    srcs = ["resolver_fuzzer.cc"],
    corpus = "resolver_fuzzer_corpus",
    language = "C++",
    proto = "resolver_fuzzer.proto",
    tags = [
        "no_mac",
        "no_windows",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:gpr",
        "//:grpc",
        "//src/core:channel_args",
        "//test/core/event_engine:aborting_event_engine",
        "//test/core/event_engine/fuzzing_event_engine",
        "//test/core/util:grpc_test_util",
        "//test/core/util:grpc_test_util_base",
    ],
)
