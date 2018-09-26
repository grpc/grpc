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
    actual = ":rbe_ubuntu1604_r340178",
)

alias(
    name = "rbe_ubuntu1604_large",
    actual = ":rbe_ubuntu1604_r340178_large",
)

# RBE Ubuntu16_04 r340178
platform(
    name = "rbe_ubuntu1604_r340178",
    constraint_values = [
        "@bazel_tools//platforms:x86_64",
        "@bazel_tools//platforms:linux",
        "@bazel_tools//tools/cpp:clang",
        "//constraints:xenial",
        "//constraints/sanitizers:support_msan",
        "//third_party/toolchains/machine-size:standard",
    ],
    remote_execution_properties = """
        properties: {
          name: "container-image"
          value:"docker://gcr.io/cloud-marketplace/google/rbe-ubuntu16-04@sha256:9bd8ba020af33edb5f11eff0af2f63b3bcb168cd6566d7b27c6685e717787928"
        }
        properties: {
          name: "gceMachineType"  # Small machines for majority of tests.
          value: "n1-highmem-2"
        }
        """,
)

# RBE Ubuntu16_04 r340178 large
platform(
    name = "rbe_ubuntu1604_r340178_large",
    constraint_values = [
        "//third_party" +
        "@bazel_tools//platforms:x86_64",
        "@bazel_tools//platforms:linux",
        "@bazel_tools//tools/cpp:clang",
        "//constraints:xenial",
        "//constraints/sanitizers:support_msan",
        "//third_party/toolchains/machine-size:large",
    ],
    remote_execution_properties = """
        properties: {
          name: "container-image"
          value:"docker://gcr.io/cloud-marketplace/google/rbe-ubuntu16-04@sha256:9bd8ba020af33edb5f11eff0af2f63b3bcb168cd6566d7b27c6685e717787928"
        }
        properties: {
          name: "gceMachineType"  # Small machines for majority of tests.
          value: "n1-standard-8"
        }
        """,
)
