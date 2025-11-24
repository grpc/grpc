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

load("//bazel:grpc_build_system.bzl", "grpc_cc_test", "grpc_package")

licenses(["notice"])

grpc_package(name = "test/cpp/performance")

grpc_cc_test(
    name = "writes_per_rpc_test",
    srcs = ["writes_per_rpc_test.cc"],
    external_deps = [
        "absl/log",
        "absl/log:check",
        "gtest",
    ],
    tags = ["no_windows"],
    deps = [
        "//:channel",
        "//:channel_create",
        "//:config",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//:grpc++_base",
        "//:grpc_transport_chttp2",
        "//:iomgr",
        "//:server",
        "//:stats",
        "//src/core:channel_args",
        "//src/core:channel_args_endpoint_config",
        "//src/core:default_event_engine",
        "//src/core:event_engine_tcp_socket_utils",
        "//src/core:grpc_check",
        "//src/core:notification",
        "//src/proto/grpc/testing:echo_cc_grpc",
        "//test/core/event_engine:event_engine_test_utils",
        "//test/core/event_engine/fuzzing_event_engine",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
    ],
)
