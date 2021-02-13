#!/usr/bin/env python2.7
# Copyright 2015 gRPC authors.
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

"""Generates the appropriate build.json data for all the end2end tests."""

load("//bazel:grpc_build_system.bzl", "grpc_cc_test")

def gen_test():
    pattern = "a0b1c2d3e4f5g6h7i8j9/k0l1m2n3o4p5q6r7s8t9/u0v1w2x3y4z5a6b7c8d9/e0f1g2h3i4j5k6l7m8n9/o0p1q2r3s4t5u6v7w8x9/y0z1a2b3c4d5e6f7g8h9i/0j1k2l3m4n5o6p7q8r9s/0t1u2v3w4x5y6z7a8b9c/0d1e2f3g4h5i6j7k8l9m/0n1o2p3q4r5s6t7u8v9w/0x1y2z"
    for i in range(1, 150):
        name = "empty_" + pattern[-i:]
        grpc_cc_test(
            name = name,
            language = "C++",
            uses_polling = False,
            deps = [
                ":empty",
            ],
        )
