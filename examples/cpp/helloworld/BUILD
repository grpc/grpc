# Copyright 2017, gRPC authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

cc_binary(
    name = "greeter_client",
    srcs = ["greeter_client.cc"],
    deps = ["//examples/protos:helloworld"],
    defines = ["BAZEL_BUILD"],
)

cc_binary(
    name = "greeter_server",
    srcs = ["greeter_server.cc"],
    deps = ["//examples/protos:helloworld"],
    defines = ["BAZEL_BUILD"],
)
