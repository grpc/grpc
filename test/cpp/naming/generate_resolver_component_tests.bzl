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

load("//bazel:grpc_build_system.bzl", "grpc_cc_binary", "grpc_cc_test")

def generate_resolver_component_tests():
    for unsecure_build_config_suffix in ["_unsecure", ""]:
        grpc_cc_test(
            name = "address_sorting_test%s" % unsecure_build_config_suffix,
            srcs = [
                "address_sorting_test.cc",
            ],
            external_deps = [
                "gtest",
            ],
            deps = [
                "//test/cpp/util:test_util%s" % unsecure_build_config_suffix,
                "//test/core/util:grpc_test_util%s" % unsecure_build_config_suffix,
                "//:grpc++%s" % unsecure_build_config_suffix,
                "//:grpc%s" % unsecure_build_config_suffix,
                "//:gpr",
                "//test/cpp/util:test_config",
            ],
            tags = ["no_windows"],
        )

        # meant to be invoked only through the top-level shell script driver
        grpc_cc_binary(
            name = "resolver_component_test%s" % unsecure_build_config_suffix,
            testonly = 1,
            srcs = [
                "resolver_component_test.cc",
            ],
            external_deps = [
                "gtest",
            ],
            deps = [
                ":dns_test_util",
                "//test/cpp/util:test_util%s" % unsecure_build_config_suffix,
                "//test/core/util:grpc_test_util%s" % unsecure_build_config_suffix,
                "//:grpc++%s" % unsecure_build_config_suffix,
                "//:grpc%s" % unsecure_build_config_suffix,
                "//:gpr",
                "//test/cpp/util:test_config",
            ],
            tags = ["no_windows"],
        )
        grpc_cc_test(
            name = "resolver_component_tests_runner_invoker%s" % unsecure_build_config_suffix,
            srcs = [
                "resolver_component_tests_runner_invoker.cc",
            ],
            external_deps = [
                "absl/flags:flag",
            ],
            deps = [
                "//test/cpp/util:test_util",
                "//test/core/util:grpc_test_util",
                "//:grpc++",
                "//:grpc",
                "//:gpr",
                "//test/cpp/util:test_config",
            ],
            data = [
                ":resolver_component_tests_runner",
                ":resolver_component_test%s" % unsecure_build_config_suffix,
                "//test/cpp/naming/utils:dns_server",
                "//test/cpp/naming/utils:dns_resolver",
                "//test/cpp/naming/utils:tcp_connect",
                "resolver_test_record_groups.yaml",  # include the transitive dependency so that the dns sever py binary can locate this
            ],
            args = [
                "--test_bin_name=resolver_component_test%s" % unsecure_build_config_suffix,
                "--running_under_bazel=true",
            ],
            tags = ["no_windows", "no_mac"],
        )
