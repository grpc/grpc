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

# Latest RBE Ubuntu16_04 container
# Update every time when a new container is released.
alias(
    name = "rbe_ubuntu1604",
    actual = ":rbe_ubuntu1604_r342117",
)

alias(
    name = "rbe_ubuntu1604_large",
    actual = ":rbe_ubuntu1604_r342117_large",
)

# RBE Ubuntu16_04 r342117
platform(
    name = "rbe_ubuntu1604_r342117",
    constraint_values = [
        "@bazel_tools//platforms:x86_64",
        "@bazel_tools//platforms:linux",
        "@bazel_tools//tools/cpp:clang",
        "@com_github_bazelbuild_bazeltoolchains//constraints:xenial",
        "@com_github_bazelbuild_bazeltoolchains//constraints/sanitizers:support_msan",
        "//third_party/toolchains/machine_size:standard",
    ],
    remote_execution_properties = """
        properties: {
          name: "container-image"
          value:"docker://gcr.io/cloud-marketplace/google/rbe-ubuntu16-04@sha256:f3120a030a19d67626ababdac79cc787e699a1aa924081431285118f87e7b375"
        }
        properties: {
          name: "gceMachineType"  # Small machines for majority of tests.
          value: "n1-highmem-2"
        }
        """,
)

# RBE Ubuntu16_04 r342117 large
platform(
    name = "rbe_ubuntu1604_r342117_large",
    constraint_values = [
        "@bazel_tools//platforms:x86_64",
        "@bazel_tools//platforms:linux",
        "@bazel_tools//tools/cpp:clang",
        "@com_github_bazelbuild_bazeltoolchains//constraints:xenial",
        "@com_github_bazelbuild_bazeltoolchains//constraints/sanitizers:support_msan",
        "//third_party/toolchains/machine_size:large",
    ],
    remote_execution_properties = """
        properties: {
          name: "container-image"
          value:"docker://gcr.io/cloud-marketplace/google/rbe-ubuntu16-04@sha256:f3120a030a19d67626ababdac79cc787e699a1aa924081431285118f87e7b375"
        }
        properties: {
          name: "gceMachineType"  # Large machines for some resource demanding tests (TSAN).
          value: "n1-standard-8"
        }
    """,
)

# This target is auto-generated from release/cpp.tpl and should not be
# modified directly.
toolchain(
    name = "cc-toolchain-clang-x86_64-default",
    exec_compatible_with = [
    ],
    target_compatible_with = [
    ],
    toolchain = "@com_github_bazelbuild_bazeltoolchains//configs/ubuntu16_04_clang/1.1/bazel_0.16.1/default:cc-compiler-k8",
    toolchain_type = "@bazel_tools//tools/cpp:toolchain_type",
)
