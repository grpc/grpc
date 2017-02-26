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

licenses(["notice"])  # Apache v2

cc_library(
    name = "test_config",
    srcs = [
        "test_config_cc.cc",
    ],
    hdrs = [
        "test_config.h",
    ],
    visibility = ["//test:__subpackages__"],
    deps = [
        "//:gpr",
        "//external:gflags",
    ],
)

cc_library(
    name = "grpc++_proto_reflection_desc_db",
    srcs = [
        "proto_reflection_descriptor_database.cc",
    ],
    hdrs = [
        "proto_reflection_descriptor_database.h",
    ],
    visibility = ["//test:__subpackages__"],
    deps = [
        "//:grpc++_config_proto",
        "//src/proto/grpc/reflection/v1alpha:reflection_proto",
    ],
)

cc_library(
    name = "test_util",
    srcs = [
        #        "test/cpp/end2end/test_service_impl.cc",
        "byte_buffer_proto_helper.cc",
        "create_test_channel.cc",
        "string_ref_helper.cc",
        "subprocess.cc",
        "test_credentials_provider.cc",
    ],
    hdrs = [
        "byte_buffer_proto_helper.h",
        "create_test_channel.h",
        "string_ref_helper.h",
        "subprocess.h",
        "test_credentials_provider.h",
    ],
    visibility = ["//test:__subpackages__"],
    deps = [
        "//:grpc++",
        "//test/core/end2end:ssl_test_data",
        "//test/core/util:gpr_test_util",
    ],
)
