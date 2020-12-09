# Copyright 2020 gRPC authors.
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

load("//bazel:grpc_build_system.bzl", "grpc_cc_test", "grpc_package")

grpc_package(name = "test/core/xds")

licenses(["notice"])

grpc_cc_test(
    name = "xds_bootstrap_test",
    srcs = ["xds_bootstrap_test.cc"],
    external_deps = [
        "gtest",
    ],
    language = "C++",
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "certificate_provider_store_test",
    srcs = ["certificate_provider_store_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "file_watcher_certificate_provider_factory_test",
    srcs = ["file_watcher_certificate_provider_factory_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "google_mesh_ca_certificate_provider_factory_test",
    srcs = ["google_mesh_ca_certificate_provider_factory_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc_google_mesh_ca_certificate_provider_factory",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "xds_certificate_provider_test",
    srcs = ["xds_certificate_provider_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc_secure",
        "//test/core/util:grpc_test_util",
    ],
)
