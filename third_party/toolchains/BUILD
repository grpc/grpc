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

licenses(["notice"])  # Apache v2

package(default_visibility = ["//visibility:public"])

alias(
    name = "rbe_windows",
    actual = ":rbe_windows_1803",
)

# RBE Windows
platform(
    name = "rbe_windows_1803",
    constraint_values = [
        "@bazel_tools//platforms:x86_64",
        "@bazel_tools//platforms:windows",
        "@bazel_tools//tools/cpp:msvc",
    ],
    remote_execution_properties = """
        properties: {
          name: "container-image"
          value:"docker://gcr.io/grpc-testing/rbe_windows_toolchain@sha256:689b177e4a157c431c7077d19d043de27922c37de835031f29c9093b8d5c6370"
        }
        properties: {
          name: "gceMachineType"  # Small machines for majority of tests.
          value: "n1-highmem-2"
        }
        properties:{
            name: "OSFamily"
            value: "Windows"
        }

        """,
)

# RBE Ubuntu16_04 r346485
platform(
    name = "rbe_ubuntu1604",
    parents = ["@rbe_default//config:platform"],
    constraint_values = [
        "//third_party/toolchains/machine_size:standard",
    ],
    remote_execution_properties = """
        {PARENT_REMOTE_EXECUTION_PROPERTIES}
        properties: {
          name: "gceMachineType"  # Small machines for majority of tests.
          value: "n1-highmem-2"
        }
        properties: {
            name: "dockerSiblingContainers"
            value: "false"
        }
        properties: {
            name: "dockerNetwork"
            value: "off"
        }
        properties: {
            name: "dockerAddCapabilities"
            value: "SYS_PTRACE"
        }
        properties: {
            name: "dockerPrivileged"
            value: "true"
        }
        """,
)

platform(
    name = "rbe_ubuntu1604_large",
    parents = ["@rbe_default//config:platform"],
    constraint_values = [
        "//third_party/toolchains/machine_size:large",
    ],
    remote_execution_properties = """
        {PARENT_REMOTE_EXECUTION_PROPERTIES}
        properties: {
          name: "gceMachineType"  # Large machines for some resource demanding tests (TSAN).
          value: "n1-standard-8"
        }
        properties: {
            name: "dockerSiblingContainers"
            value: "false"
        }
        properties: {
            name: "dockerNetwork"
            value: "off"
        }
        properties: {
            name: "dockerAddCapabilities"
            value: "SYS_PTRACE"
        }
        properties: {
            name: "dockerPrivileged"
            value: "true"
        }
    """,
)

platform(
    name = "local",
    parents = ["@bazel_tools//platforms:target_platform"],
    constraint_values = [
        "//third_party/toolchains/machine_size:standard",
    ],
)

platform(
    name = "local_large",
    parents = ["@bazel_tools//platforms:target_platform"],
    constraint_values = [
        "//third_party/toolchains/machine_size:large",
    ],
)
