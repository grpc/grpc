# Copyright 2021 gRPC authors.
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

cc_library(
    name = "libprotobuf_mutator",
    srcs = glob(
        ["src/*.cc", "src/libfuzzer/*.cc"], 
        exclude = ["src/*_test.cc", "src/libfuzzer/*_test.cc"]
    ),
    hdrs = glob(["src/*.h", "port/*.h", "src/libfuzzer/*.h"]),
    deps = [
        "@com_google_protobuf//:protobuf",
        "@com_google_googletest//:gtest",
    ],
    visibility = ["//visibility:public"],
    includes = ["."]
)
