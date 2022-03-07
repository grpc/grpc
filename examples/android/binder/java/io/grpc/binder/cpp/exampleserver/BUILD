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

load("@build_bazel_rules_android//android:rules.bzl", "android_binary", "android_library")

cc_library(
    name = "jni_lib",
    srcs = ["native.cc"],
    linkopts = [
        "-ldl",
        "-llog",
        "-lm",
        "-Wl,--no-undefined",
    ],
    deps = [
        "//:grpc++",
        "//examples/protos:helloworld_cc_grpc",
    ],
    alwayslink = True,
)

android_library(
    name = "activity",
    srcs = [
        "ButtonPressHandler.java",
        "MainActivity.java",
    ],
    manifest = "AndroidManifest.xml",
    resource_files = glob(["res/**"]),
    deps = [
        ":endpoint",
        ":jni_lib",
    ],
)

android_library(
    name = "endpoint",
    srcs = ["ExportedEndpointService.java"],
    exports_manifest = True,
    manifest = "AndroidManifest_endpoint.xml",
    deps = [
        "@binder_transport_android_helper//io/grpc/binder/cpp:connection_helper",
    ],
)

android_binary(
    name = "app",
    manifest = "AndroidManifest.xml",
    deps = [
        ":activity",
    ],
)
