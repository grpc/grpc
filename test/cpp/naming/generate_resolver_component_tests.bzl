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

load("//bazel:grpc_build_system.bzl", "grpc_sh_binary", "grpc_cc_test", "grpc_cc_binary")

def generate_resolver_component_tests():
  for unsecure_build_config_suffix in ['_unsecure', '']:
    # meant to be invoked only through the top-level shell script driver
    grpc_cc_binary(
        name = "resolver_component_test%s" % unsecure_build_config_suffix,
        testonly = 1,
        srcs = [
            "resolver_component_test.cc",
        ],
        external_deps = [
            "gmock",
        ],
        deps = [
            "//test/cpp/util:test_util%s" % unsecure_build_config_suffix,
            "//test/core/util:grpc_test_util%s" % unsecure_build_config_suffix,
            "//test/core/util:gpr_test_util",
            "//:grpc++%s" % unsecure_build_config_suffix,
            "//:grpc%s" % unsecure_build_config_suffix,
            "//:gpr",
            "//test/cpp/util:test_config",
        ],
    )
    grpc_cc_test(
        name = "resolver_component_tests_runner_invoker%s" % unsecure_build_config_suffix,
        srcs = [
            "resolver_component_tests_runner_invoker.cc",
        ],
        deps = [
            "//test/cpp/util:test_util",
            "//test/core/util:grpc_test_util",
            "//test/core/util:gpr_test_util",
            "//:grpc++",
            "//:grpc",
            "//:gpr",
            "//test/cpp/util:test_config",
        ],
        data = [
            ":resolver_component_tests_runner",
            ":resolver_component_test%s" % unsecure_build_config_suffix,
            ":test_dns_server",
            "resolver_test_record_groups.yaml", # include the transitive dependency so that the dns sever py binary can locate this
        ],
        args = [
            "--test_bin_name=resolver_component_test%s" % unsecure_build_config_suffix,
            "--running_under_bazel=true",
        ]
    )
