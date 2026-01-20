#!/usr/bin/env python2.7
# Copyright 2015 gRPC authors.
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

"""Houses generate_resolver_component_tests."""

load(
    "//bazel:grpc_build_system.bzl",
    "grpc_cc_binary",
    "grpc_cc_test",
)

# buildifier: disable=unnamed-macro
def generate_resolver_component_tests():
    """Generate address_sorting_test and resolver_component_test suite with different configurations.

    Note that the resolver_component_test suite's configuration is 2 dimensional:
    security and whether to enable the event_engine_dns experiment.
    """
    for unsecure_build_config_suffix in ["_unsecure", ""]:
        grpc_cc_test(
            name = "address_sorting_test%s" % unsecure_build_config_suffix,
            srcs = [
                "address_sorting_test.cc",
            ],
            external_deps = [
                "absl/log",
                "absl/log:check",
                "gtest",
            ],
            deps = [
                "//test/cpp/util:test_util%s" %
                unsecure_build_config_suffix,
                "//test/core/test_util:grpc_test_util%s" %
                unsecure_build_config_suffix,
                "//:grpc++%s" % unsecure_build_config_suffix,
                "//:grpc%s" % unsecure_build_config_suffix,
                "//:gpr",
                "//test/cpp/util:test_config",
                "//third_party/address_sorting",
                "//:config_vars",
                "//:endpoint_addresses",
                "//:exec_ctx",
                "//:grpc_client_channel",
                "//:grpc_resolver",
                "//:grpc_resolver_dns_ares",
                "//:iomgr",
                "//:sockaddr_utils",
                "//src/core:channel_args",
                "//src/core:grpc_check",
            ],
            tags = [
                "no_windows",
                # relies on ares
                "grpc:broken-internally",
            ],
        )

        # meant to be invoked only through the top-level shell script driver
        grpc_cc_binary(
            name = "resolver_component_test%s" % unsecure_build_config_suffix,
            testonly = 1,
            srcs = [
                "resolver_component_test.cc",
            ],
            external_deps = [
                "absl/flags:flag",
                "absl/log:check",
                "absl/log:log",
                "absl/memory",
                "absl/strings",
                "absl/strings:str_format",
                "gtest",
            ],
            deps = [
                "//test/cpp/util:test_util%s" %
                unsecure_build_config_suffix,
                "//test/core/test_util:grpc_test_util%s" %
                unsecure_build_config_suffix,
                "//test/core/test_util:fake_udp_and_tcp_server%s" %
                unsecure_build_config_suffix,
                "//test/core/test_util:socket_use_after_close_detector%s" %
                unsecure_build_config_suffix,
                "//:grpc++%s" % unsecure_build_config_suffix,
                "//:grpc%s" % unsecure_build_config_suffix,
                "//:gpr",
                "//src/core:ares_resolver",
                "//test/cpp/util:test_config",
                "//:config",
                "//:endpoint_addresses",
                "//:grpc_client_channel",
                "//:grpc_grpclb_balancer_addresses",
                "//:grpc_resolver",
                "//:grpc_resolver_dns_ares",
                "//:iomgr",
                "//:orphanable",
                "//:parse_address",
                "//:sockaddr_utils",
                "//:work_serializer",
                "//src/core:channel_args",
                "//src/core:default_event_engine",
                "//src/core:experiments",
                "//src/core:grpc_check",
                "//src/core:grpc_sockaddr",
            ],
            tags = ["no_windows", "grpc:broken-internally"],
        )
        grpc_cc_test(
            name = "resolver_component_tests_runner_invoker%s" %
                   unsecure_build_config_suffix,
            srcs = [
                "resolver_component_tests_runner_invoker.cc",
            ],
            external_deps = [
                "absl/flags:flag",
                "absl/log:check",
                "absl/strings",
                "absl/log",
                "absl/strings:str_format",
            ],
            deps = [
                "//test/cpp/util:test_util%s" %
                unsecure_build_config_suffix,
                "//test/core/test_util:grpc_test_util%s" %
                unsecure_build_config_suffix,
                "//:grpc++%s" % unsecure_build_config_suffix,
                "//:grpc%s" % unsecure_build_config_suffix,
                "//:gpr",
                "//src/core:env",
                "//test/cpp/util:test_config",
                "//test/cpp/util/windows:manifest_file",
                "//src/core:grpc_check",
            ],
            data = [
                ":resolver_component_tests_runner",
                ":resolver_component_test%s" % unsecure_build_config_suffix,
                "//test/cpp/naming/utils:dns_server",
                "//test/cpp/naming/utils:dns_resolver",
                "//test/cpp/naming/utils:tcp_connect",
                "//test/cpp/naming:resolver_test_record_groups",  # include the transitive dependency so that the dns server py binary can locate this
            ],
            args = [
                "--test_bin_name=resolver_component_test%s" %
                unsecure_build_config_suffix,
                "--running_under_bazel=true",
            ],
            tags = [
                "no_mac",
                "requires-net:ipv4",
                "requires-net:loopback",
                "resolver_component_tests_runner_invoker",
                "grpc:broken-internally",
            ],
        )
