# Copyright 2016 gRPC authors.
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

load("//bazel:grpc_build_system.bzl", "grpc_cc_library", "grpc_cc_test", "grpc_cc_binary")

licenses(["notice"])  # Apache v2

package(
    features = [
        "-layering_check",
        "-parse_headers",
    ],
)

grpc_cc_test(
    name = "context_test",
    srcs = ["context_test.c"],
    language = "C",
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:gpr_test_util",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "mlog_test",
    srcs = ["mlog_test.c"],
    language = "C",
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:gpr_test_util",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "resource_test",
    srcs = ["resource_test.c"],
    language = "C",
    data = [
        ":data/resource_empty_name.pb",
        ":data/resource_full.pb",
        ":data/resource_minimal_good.pb",
        ":data/resource_no_name.pb",
        ":data/resource_no_numerator.pb",
        ":data/resource_no_unit.pb",
    ],
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:gpr_test_util",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "trace_context_test",
    srcs = ["trace_context_test.c"],
    language = "C",
    data = [
        ":data/context_empty.pb",
        ":data/context_full.pb",
        ":data/context_no_span_options.pb",
        ":data/context_span_only.pb",
        ":data/context_trace_only.pb",
    ],
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:gpr_test_util",
        "//test/core/util:grpc_test_util",
    ],
)
