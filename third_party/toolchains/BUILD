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
    exec_properties = {
        "container-image" : "docker://gcr.io/grpc-testing/rbe_windows_toolchain@sha256:75728e7d6d804090f71095e5fec627b18cfa1f47adc52096c209f2a687e06b2e",
        "gceMachineType" : "n1-highmem-2",
        "OSFamily" : "Windows",
    },
)

# RBE Ubuntu16_04 r346485
platform(
    name = "rbe_ubuntu1604",
    parents = ["@rbe_default//config:platform"],
    constraint_values = [
        "//third_party/toolchains/machine_size:standard",
    ],
    exec_properties = {
        "gceMachineType" : "n1-highmem-2",  # Small machines for majority of tests
        "dockerSiblingContainers" : "false",
        "dockerNetwork" : "off",
        "dockerAddCapabilities" : "SYS_PTRACE",
        "dockerPrivileged" : "true"
    },
)

platform(
    name = "rbe_ubuntu1604_large",
    parents = ["@rbe_default//config:platform"],
    constraint_values = [
        "//third_party/toolchains/machine_size:large",
    ],
    exec_properties = {
        "gceMachineType" : "n1-standard-8",  # Large machines for some resource demanding tests (TSAN).
        "dockerSiblingContainers" : "false",
        "dockerNetwork" : "off",
        "dockerAddCapabilities" : "SYS_PTRACE",
        "dockerPrivileged" : "true"
    },
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
