# Copyright 2025 The gRPC Authors
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

cc_library(
    name = "extract_metadata_from_bazel_xml",
    srcs = ["extract_metadata_from_bazel_xml.cc"],
    hdrs = ["extract_metadata_from_bazel_xml.h"],
    deps = [
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings",
        "@nlohmann_json//:json",
        "@pugixml",
    ],
)

cc_library(
    name = "metadata_for_wrapped_languages",
    srcs = ["metadata_for_wrapped_languages.cc"],
    hdrs = ["metadata_for_wrapped_languages.h"],
    deps = [
        "utils",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings",
        "@nlohmann_json//:json",
    ],
)

cc_library(
    name = "boringssl",
    srcs = ["boringssl.cc"],
    hdrs = ["boringssl.h"],
    deps = [
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings",
        "@nlohmann_json//:json",
    ],
)

cc_library(
    name = "utils",
    srcs = ["utils.cc"],
    hdrs = ["utils.h"],
    deps = [
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@nlohmann_json//:json",
        "@yaml-cpp//:yaml-cpp",
    ],
)

cc_library(
    name = "render",
    srcs = ["render.cc"],
    hdrs = ["render.h"],
    deps = [
        ":utils",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings",
        "@nlohmann_json//:json",
        "@pantor_inja//:inja",
    ],
)

cc_binary(
    name = "artifact_gen",
    srcs = ["artifact_gen.cc"],
    deps = [
        ":extract_metadata_from_bazel_xml",
        ":metadata_for_wrapped_languages",
        ":render",
        ":utils",
        "@abseil-cpp//absl/flags:parse",
        "@pantor_inja//:inja",
    ],
)
