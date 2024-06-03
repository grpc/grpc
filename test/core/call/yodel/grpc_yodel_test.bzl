# Copyright 2024 gRPC authors.
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

"""
Generate one transport test & associated fuzzer
"""

load("//bazel:grpc_build_system.bzl", "grpc_cc_library", "grpc_cc_test")
load("//test/core/test_util:grpc_fuzzer.bzl", "grpc_proto_fuzzer")

def grpc_yodel_test(name, deps):
    grpc_cc_test(
        name = name + "_test",
        srcs = [],
        tags = ["no_windows", "no_mac"],
        deps = [
            "//test/core/call/yodel:test_main",
        ] + deps,
        uses_polling = False,
    )

    grpc_proto_fuzzer(
        name = name + "_fuzzer",
        srcs = ["//test/core/call/yodel:fuzzer_main.cc"],
        tags = ["no_windows", "no_mac"],
        external_deps = [
            "absl/log:check",
            "gtest",
        ],
        deps = [
            "//test/core/call/yodel:yodel_test",
            "//test/core/call/yodel:fuzzer_proto",
            "//:event_engine_base_hdrs",
            "//:config_vars",
            "//:exec_ctx",
            "//:gpr",
            "//:grpc_unsecure",
            "//:iomgr_timer",
            "//src/core:default_event_engine",
            "//src/core:env",
            "//src/core:experiments",
            "//test/core/event_engine/fuzzing_event_engine",
            "//test/core/test_util:fuzz_config_vars",
            "//test/core/test_util:proto_bit_gen",
        ] + deps,
        corpus = "corpus/%s" % name,
        proto = None,
    )

def grpc_yodel_simple_test(name, **kwargs):
    grpc_cc_library(
        name = "%s_test_lib" % name,
        testonly = True,
        alwayslink = 1,
        **kwargs
    )
    grpc_yodel_test(name, deps = ["%s_test_lib" % name])
