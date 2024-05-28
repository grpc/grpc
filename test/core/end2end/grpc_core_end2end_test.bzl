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

load("//bazel:grpc_build_system.bzl", "grpc_cc_library", "grpc_cc_test")
load("//test/core/test_util:grpc_fuzzer.bzl", "grpc_proto_fuzzer")

END2END_TEST_DATA = [
    "//src/core/tsi/test_creds:ca.pem",
    "//src/core/tsi/test_creds:server1.key",
    "//src/core/tsi/test_creds:server1.pem",
]

def grpc_core_end2end_test(name, shard_count = 10, tags = []):
    """Generate one core end2end test

    Args:
        name: name of the test, must correspond to a "test/name.cc" file
        shard_count: per bazel
        tags: per bazel
    """

    if len(name) > 60:
        fail("test name %s too long" % name)

    grpc_cc_library(
        name = "%s_library" % name,
        testonly = 1,
        srcs = [
            "tests/%s.cc" % name,
        ],
        external_deps = [
            "absl/log:log",
        ],
        deps = [
            "cq_verifier",
            "end2end_test_lib",
            "fixture_support",
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
            "//:grpc_unsecure",
            "//:legacy_context",
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
        ],
    )

    grpc_cc_test(
        name = "%s_test" % name,
        shard_count = shard_count,
        data = END2END_TEST_DATA,
        external_deps = [
            "absl/functional:any_invocable",
            "absl/status",
            "absl/status:statusor",
            "absl/strings",
            "absl/strings:str_format",
            "absl/types:optional",
            "gtest",
        ],
        deps = [
            "end2end_test_main",
            "%s_library" % name,
        ],
        tags = ["core_end2end_test"] + tags,
    )

    grpc_proto_fuzzer(
        name = "%s_fuzzer" % name,
        srcs = [
            "end2end_test_fuzzer_main.cc",
        ],
        corpus = "end2end_test_corpus/%s" % name,
        data = END2END_TEST_DATA,
        external_deps = [
            "absl/functional:any_invocable",
            "absl/status",
            "absl/status:statusor",
            "absl/strings",
            "absl/strings:str_format",
            "absl/types:optional",
            "gtest",
        ],
        language = "C++",
        proto = None,
        tags = [
            "no_mac",
            "no_windows",
        ],
        uses_event_engine = False,
        uses_polling = False,
        deps = [
            "%s_library" % name,
            "end2end_test_fuzzer",
        ],
    )
