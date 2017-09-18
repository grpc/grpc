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

package(
    default_visibility = ["//visibility:public"],
    features = [
        "-layering_check",
        "-parse_headers",
    ],
)

licenses(["notice"])  # Apache v2

load("//bazel:grpc_build_system.bzl", "grpc_sh_binary", "grpc_py_binary")

load(":generate_resolver_component_tests.bzl", "generate_resolver_component_tests")

# Meant to be invoked only through the top-level shell script driver.
grpc_sh_binary(
    name = "resolver_component_tests_runner",
    srcs = [
        "resolver_component_tests_runner.sh",
    ],
)

grpc_py_binary(
  name = "test_dns_server",
  srcs = ["test_dns_server.py"],
  data = [
      "resolver_test_record_groups.yaml",
  ],
  deps = [
      "twisted",
      "yaml",
  ]
)

generate_resolver_component_tests()
