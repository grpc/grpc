# Copyright 2016, gRPC authors
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

licenses(["notice"])  # Apache v2

cc_binary(
    name = "low_level_ping_pong",
    srcs = ["low_level_ping_pong.c"],
    deps = ["//:grpc", "//test/core/util:grpc_test_util", "//:gpr", "//test/core/util:gpr_test_util"],
    copts = ['-std=c99']
)
