# gRPC Bazel BUILD file.
#
# Copyright 2021 The gRPC authors.
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

proto_library(
    name = "my_proto",
    srcs = ["my.proto"],
    visibility = ["//visibility:public"],
)

genrule(
    name = "make_my_proto",
    outs = ["my.proto"],
    cmd = """
      echo -e "syntax = \\"proto3\\";\npackage somewhere_else;\nmessage MyMessage {};" > $@
    """,
)
