# Copyright 2023 gRPC authors.
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

licenses(["notice"])

package(
    default_visibility = ["//visibility:public"]
)

# This is needed for the dependency on google_cloud_cpp to work.
# Taken from https://github.com/googleapis/google-cloud-cpp/blob/2839e9dba793ca023e11ea67f201f66f74fa7d3e/bazel/googleapis.BUILD
cc_library(
    name = "googleapis_system_includes",
    includes = [
        ".",
    ],
)
