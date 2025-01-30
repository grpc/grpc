# Copyright 2016 gRPC authors.
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
Generate one e2e test & associated fuzzer
"""

load("//bazel:grpc_build_system.bzl", "grpc_cc_test")
load(
    "//test/core/test_util:grpc_fuzzer.bzl",
    "grpc_fuzz_test",
)

_DATA = [
    "//src/core/tsi/test_creds:ca.pem",
    "//src/core/tsi/test_creds:server1.key",
    "//src/core/tsi/test_creds:server1.pem",
]

_DEPS = [
    "cq_verifier",
    "http_proxy",
    "proxy",
    "//:channel_stack_builder",
    "//:config",
    "//:config_vars",
    "//:debug_location",
    "//:exec_ctx",
    "//:gpr",
    "//:grpc_authorization_provider",
    "//:grpc_public_hdrs",
    "//:grpc_security_base",
    "//:grpc_trace",
    "//:grpc",
    "//:orphanable",
    "//:promise",
    "//:ref_counted_ptr",
    "//:stats",
    "//src/core:arena_promise",
    "//src/core:bitset",
    "//src/core:channel_args",
    "//src/core:channel_fwd",
    "//src/core:channel_init",
    "//src/core:channel_stack_type",
    "//src/core:closure",
    "//src/core:error",
    "//src/core:experiments",
    "//src/core:grpc_authorization_base",
    "//src/core:grpc_fake_credentials",
    "//src/core:iomgr_port",
    "//src/core:json",
    "//src/core:lb_policy",
    "//src/core:lb_policy_factory",
    "//src/core:no_destruct",
    "//src/core:notification",
    "//src/core:slice",
    "//src/core:stats_data",
    "//src/core:status_helper",
    "//src/core:time",
    "//test/core/test_util:fake_stats_plugin",
    "//test/core/test_util:grpc_test_util",
    "//test/core/test_util:test_lb_policies",
]

def grpc_core_end2end_test(name, shard_count = 1, enable_fuzzing = True, tags = [], flaky = False):
    """Generate one core end2end test

    Args:
        name: name of the test, must correspond to a "test/name.cc" file
        shard_count: per bazel
        tags: per bazel
        flaky: per bazel
        enable_fuzzing: also create a fuzzer
    """

    if len(name) > 60:
        fail("test name %s too long" % name)

    grpc_cc_test(
        name = name + "_test",
        srcs = [
            "tests/%s.cc" % name,
        ],
        external_deps = [
            "absl/log:log",
            "gtest",
            "gtest_main",
        ],
        deps = _DEPS + ["end2end_test_lib_no_fuzztest_gtest"],
        data = _DATA,
        shard_count = shard_count,
        tags = tags + ["core_end2end_test"],
        flaky = flaky,
    )

    if enable_fuzzing:
        grpc_fuzz_test(
            name = name + "_fuzzer",
            srcs = [
                "tests/%s.cc" % name,
            ],
            external_deps = [
                "absl/log:log",
                "gtest",
                "fuzztest",
                "fuzztest_main",
            ],
            deps = _DEPS + ["end2end_test_lib_fuzztest_no_gtest"],
            data = _DATA,
        )
