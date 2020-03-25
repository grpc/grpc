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

load("@bazel_toolchains//rules/exec_properties:exec_properties.bzl", "create_exec_properties_dict")

alias(
    name = "rbe_windows",
    actual = ":rbe_windows_2019",
)

# RBE Windows
platform(
    name = "rbe_windows_2019",
    constraint_values = [
        "@bazel_tools//platforms:x86_64",
        "@bazel_tools//platforms:windows",
    ],
    exec_properties = create_exec_properties_dict(
        # See rbe_win2019/Dockerfile for image details
        container_image = "docker://gcr.io/grpc-testing/rbe_windows2019_withdbg_toolchain@sha256:7b04ee7e29f942adbf4f70edd2ec4ba20a3e7237e1b54f5cae4b239c6ca41105",
        
        # Use a different machine type than used on linux to avoid accidentally scheduling linux jobs on windows workers and vice versa on older release branches
        gce_machine_type = "n1-standard-2",
        os_family = "Windows",
        # labels only supported starting from https://github.com/bazelbuild/bazel-toolchains/pull/748
        #labels = {"os": "windows_2019"},
    ),
)
