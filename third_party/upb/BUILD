# Copyright (c) 2009-2021, Google LLC
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in the
#       documentation and/or other materials provided with the distribution.
#     * Neither the name of Google LLC nor the
#       names of its contributors may be used to endorse or promote products
#       derived from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL Google LLC BE LIABLE FOR ANY
# DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
# ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

load(
    "//bazel:build_defs.bzl",
    "UPB_DEFAULT_COPTS",
    "UPB_DEFAULT_CPPOPTS",
    "make_shell_script",
)
load(
    "//bazel:upb_proto_library.bzl",
    "upb_fasttable_enabled",
    "upb_proto_library",
    "upb_proto_library_copts",
    "upb_proto_reflection_library",
)

# begin:github_only
load(
    "//bazel:build_defs.bzl",
    "upb_amalgamation",
)
# end:github_only

licenses(["notice"])

exports_files(["LICENSE"])

exports_files(
    [
        "BUILD",
        "WORKSPACE",
    ],
    visibility = ["//cmake:__pkg__"],
)

config_setting(
    name = "windows",
    constraint_values = ["@bazel_tools//platforms:windows"],
    visibility = ["//visibility:public"],
)

upb_fasttable_enabled(
    name = "fasttable_enabled",
    build_setting_default = False,
    visibility = ["//visibility:public"],
)

config_setting(
    name = "fasttable_enabled_setting",
    flag_values = {"//:fasttable_enabled": "true"},
)

upb_proto_library_copts(
    name = "upb_proto_library_copts__for_generated_code_only_do_not_use",
    copts = UPB_DEFAULT_COPTS,
    visibility = ["//visibility:public"],
)

# Public C/C++ libraries #######################################################

cc_library(
    name = "port",
    copts = UPB_DEFAULT_COPTS,
    textual_hdrs = [
        "upb/port_def.inc",
        "upb/port_undef.inc",
    ],
    visibility = ["//:__subpackages__"],
)

cc_library(
    name = "upb",
    srcs = [
        "upb/decode.c",
        "upb/decode_internal.h",
        "upb/encode.c",
        "upb/msg.c",
        "upb/msg_internal.h",
        "upb/table.c",
        "upb/table_internal.h",
        "upb/upb.c",
        "upb/upb_internal.h",
    ],
    hdrs = [
        "upb/decode.h",
        "upb/encode.h",
        "upb/msg.h",
        "upb/upb.h",
        "upb/upb.hpp",
    ],
    copts = UPB_DEFAULT_COPTS,
    visibility = ["//visibility:public"],
    deps = [
        ":fastdecode",
        ":port",
        "//third_party/utf8_range",
    ],
)

cc_library(
    name = "mini_table_internal",
    hdrs = ["upb/msg_internal.h"],
    deps = [
        ":port",
        ":table",
        ":upb",
    ],
)

cc_library(
    name = "mini_table",
    srcs = ["upb/mini_table.c"],
    hdrs = [
        "upb/mini_table.h",
        "upb/mini_table.hpp",
    ],
    copts = UPB_DEFAULT_COPTS,
    visibility = ["//visibility:public"],
    deps = [
        ":mini_table_internal",
        ":port",
        ":upb",
    ],
)

cc_test(
    name = "mini_table_test",
    srcs = ["upb/mini_table_test.cc"],
    deps = [
        ":mini_table",
        ":mini_table_internal",
        ":upb",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_googletest//:gtest_main",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_library(
    name = "fastdecode",
    srcs = [
        "upb/decode.h",
        "upb/decode_fast.c",
        "upb/decode_fast.h",
        "upb/decode_internal.h",
        "upb/msg.h",
        "upb/msg_internal.h",
        "upb/upb_internal.h",
    ],
    copts = UPB_DEFAULT_COPTS,
    deps = [
        ":port",
        ":table",
        "//third_party/utf8_range",
    ],
)

# Common support routines used by generated code.  This library has no
# implementation, but depends on :upb and exposes a few more hdrs.
#
# This is public only because we have no way of visibility-limiting it to
# upb_proto_library() only.  This interface is not stable and by using it you
# give up any backward compatibility guarantees.
cc_library(
    name = "generated_code_support__only_for_generated_code_do_not_use__i_give_permission_to_break_me",
    hdrs = [
        "upb/decode.h",
        "upb/decode_fast.h",
        "upb/encode.h",
        "upb/msg.h",
        "upb/msg_internal.h",
        "upb/port_def.inc",
        "upb/port_undef.inc",
    ],
    copts = UPB_DEFAULT_COPTS,
    visibility = ["//visibility:public"],
    deps = [
        ":table",
        ":upb",
    ],
)

cc_library(
    name = "generated_reflection_support__only_for_generated_code_do_not_use__i_give_permission_to_break_me",
    hdrs = [
        "upb/def.h",
        "upb/port_def.inc",
        "upb/port_undef.inc",
    ],
    copts = UPB_DEFAULT_COPTS,
    visibility = ["//visibility:public"],
    deps = [
        ":descriptor_upb_proto",
        ":reflection",
        ":table",
    ],
)

upb_proto_library(
    name = "descriptor_upb_proto",
    visibility = ["//visibility:public"],
    deps = ["@com_google_protobuf//:descriptor_proto"],
)

upb_proto_reflection_library(
    name = "descriptor_upb_proto_reflection",
    visibility = ["//visibility:public"],
    deps = ["@com_google_protobuf//:descriptor_proto"],
)

cc_library(
    name = "reflection",
    srcs = [
        "upb/def.c",
        "upb/msg.h",
        "upb/reflection.c",
    ],
    hdrs = [
        "upb/def.h",
        "upb/def.hpp",
        "upb/reflection.h",
        "upb/reflection.hpp",
    ],
    copts = UPB_DEFAULT_COPTS,
    visibility = ["//visibility:public"],
    deps = [
        ":descriptor_upb_proto",
        ":port",
        ":table",
        ":upb",
    ],
)

cc_library(
    name = "textformat",
    srcs = [
        "upb/text_encode.c",
        "upb/upb_internal.h",
    ],
    hdrs = [
        "upb/text_encode.h",
    ],
    copts = UPB_DEFAULT_COPTS,
    visibility = ["//visibility:public"],
    deps = [
        ":port",
        ":reflection",
        ":table",
    ],
)

cc_library(
    name = "json",
    srcs = [
        "upb/json_decode.c",
        "upb/json_encode.c",
        "upb/upb_internal.h",
    ],
    hdrs = [
        "upb/json_decode.h",
        "upb/json_encode.h",
    ],
    copts = UPB_DEFAULT_COPTS,
    visibility = ["//visibility:public"],
    deps = [
        ":port",
        ":reflection",
        ":upb",
    ],
)

# Tests ########################################################################

cc_test(
    name = "test_generated_code",
    srcs = ["upb/test_generated_code.cc"],
    deps = [
        ":empty_upbdefs_proto",
        ":test_messages_proto2_proto_upb",
        ":test_messages_proto3_proto_upb",
        ":test_upb_proto",
        ":upb",
        "@com_google_googletest//:gtest_main",
    ],
)

proto_library(
    name = "test_proto",
    testonly = 1,
    srcs = ["upb/test.proto"],
)

upb_proto_library(
    name = "test_upb_proto",
    testonly = 1,
    deps = [":test_proto"],
)

proto_library(
    name = "empty_proto",
    srcs = ["upb/empty.proto"],
)

upb_proto_reflection_library(
    name = "empty_upbdefs_proto",
    testonly = 1,
    deps = [":empty_proto"],
)

upb_proto_library(
    name = "test_messages_proto2_proto_upb",
    testonly = 1,
    deps = ["@com_google_protobuf//:test_messages_proto2_proto"],
)

upb_proto_library(
    name = "test_messages_proto3_proto_upb",
    testonly = 1,
    deps = ["@com_google_protobuf//:test_messages_proto3_proto"],
)

cc_test(
    name = "msg_test",
    srcs = ["upb/msg_test.cc"],
    deps = [
        ":json",
        ":msg_test_upb_proto",
        ":msg_test_upb_proto_reflection",
        ":reflection",
        ":test_messages_proto3_proto_upb",
        ":upb",
        "@com_google_googletest//:gtest_main",
    ],
)

proto_library(
    name = "msg_test_proto",
    testonly = 1,
    srcs = ["upb/msg_test.proto"],
    deps = ["@com_google_protobuf//:test_messages_proto3_proto"],
)

upb_proto_library(
    name = "msg_test_upb_proto",
    testonly = 1,
    deps = [":msg_test_proto"],
)

upb_proto_reflection_library(
    name = "msg_test_upb_proto_reflection",
    testonly = 1,
    deps = [":msg_test_proto"],
)

proto_library(
    name = "test_cpp_proto",
    srcs = ["upb/test_cpp.proto"],
    deps = ["@com_google_protobuf//:timestamp_proto"],
)

upb_proto_library(
    name = "test_cpp_upb_proto",
    deps = ["test_cpp_proto"],
)

upb_proto_reflection_library(
    name = "test_cpp_upb_proto_reflection",
    deps = ["test_cpp_proto"],
)

upb_proto_library(
    name = "timestamp_upb_proto",
    deps = ["@com_google_protobuf//:timestamp_proto"],
)

upb_proto_reflection_library(
    name = "timestamp_upb_proto_reflection",
    deps = ["@com_google_protobuf//:timestamp_proto"],
)

cc_test(
    name = "test_cpp",
    srcs = ["upb/test_cpp.cc"],
    copts = UPB_DEFAULT_CPPOPTS,
    deps = [
        ":test_cpp_upb_proto",
        ":test_cpp_upb_proto_reflection",
        ":timestamp_upb_proto",
        ":timestamp_upb_proto_reflection",
        "//:json",
        "//:port",
        "//:reflection",
        "//:upb",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "test_table",
    srcs = ["upb/test_table.cc"],
    copts = UPB_DEFAULT_CPPOPTS,
    deps = [
        "//:port",
        "//:table",
        "//:upb",
        "@com_google_googletest//:gtest_main",
    ],
)

upb_proto_library(
    name = "conformance_proto_upb",
    testonly = 1,
    deps = ["@com_google_protobuf//:conformance_proto"],
)

upb_proto_reflection_library(
    name = "conformance_proto_upbdefs",
    testonly = 1,
    deps = ["@com_google_protobuf//:conformance_proto"],
)

upb_proto_reflection_library(
    name = "test_messages_proto2_upbdefs",
    testonly = 1,
    deps = ["@com_google_protobuf//:test_messages_proto2_proto"],
)

upb_proto_reflection_library(
    name = "test_messages_proto3_upbdefs",
    testonly = 1,
    deps = ["@com_google_protobuf//:test_messages_proto3_proto"],
)

cc_binary(
    name = "conformance_upb",
    testonly = 1,
    srcs = ["upb/conformance_upb.c"],
    copts = UPB_DEFAULT_COPTS,
    data = ["upb/conformance_upb_failures.txt"],
    deps = [
        ":conformance_proto_upb",
        ":conformance_proto_upbdefs",
        ":test_messages_proto2_upbdefs",
        ":test_messages_proto3_upbdefs",
        "//:json",
        "//:port",
        "//:reflection",
        "//:textformat",
        "//:upb",
    ],
)

make_shell_script(
    name = "gen_test_conformance_upb",
    out = "test_conformance_upb.sh",
    contents = "external/com_google_protobuf/conformance_test_runner " +
               " --enforce_recommended " +
               " --failure_list ./upb/conformance_upb_failures.txt" +
               " ./conformance_upb",
)

sh_test(
    name = "test_conformance_upb",
    srcs = ["test_conformance_upb.sh"],
    data = [
        "upb/conformance_upb_failures.txt",
        ":conformance_upb",
        "@com_google_protobuf//:conformance_test_runner",
    ],
    deps = ["@bazel_tools//tools/bash/runfiles"],
)

cc_binary(
    name = "conformance_upb_dynamic_minitable",
    testonly = 1,
    srcs = ["upb/conformance_upb.c"],
    copts = UPB_DEFAULT_COPTS + [
        "-DREBUILD_MINITABLES",
    ],
    data = ["upb/conformance_upb_failures.txt"],
    deps = [
        ":conformance_proto_upb",
        ":conformance_proto_upbdefs",
        ":test_messages_proto2_upbdefs",
        ":test_messages_proto3_upbdefs",
        "//:json",
        "//:port",
        "//:reflection",
        "//:textformat",
        "//:upb",
    ],
)

make_shell_script(
    name = "gen_test_conformance_upb_dynamic_minitable",
    out = "test_conformance_upb_dynamic_minitable.sh",
    contents = "external/com_google_protobuf/conformance_test_runner " +
               " --enforce_recommended " +
               " --failure_list ./upb/conformance_upb_failures.txt" +
               " ./conformance_upb_dynamic_minitable",
)

sh_test(
    name = "test_conformance_upb_dynamic_minitable",
    srcs = ["test_conformance_upb_dynamic_minitable.sh"],
    data = [
        "upb/conformance_upb_failures.txt",
        ":conformance_upb_dynamic_minitable",
        "@com_google_protobuf//:conformance_test_runner",
    ],
    deps = ["@bazel_tools//tools/bash/runfiles"],
)

# Internal C/C++ libraries #####################################################

cc_library(
    name = "table",
    hdrs = [
        "upb/table_internal.h",
        "upb/upb.h",
    ],
    visibility = [
        "//python:__pkg__",
        "//tests:__pkg__",
    ],
    deps = [
        ":port",
    ],
)

# Amalgamation #################################################################

# begin:github_only

upb_amalgamation(
    name = "gen_amalgamation",
    outs = [
        "upb.c",
        "upb.h",
    ],
    libs = [
        ":upb",
        ":fastdecode",
        ":descriptor_upb_proto",
        ":reflection",
        ":port",
    ],
)

cc_library(
    name = "amalgamation",
    srcs = ["upb.c"],
    hdrs = ["upb.h"],
    copts = UPB_DEFAULT_COPTS,
    deps = ["//third_party/utf8_range"],
)

upb_amalgamation(
    name = "gen_php_amalgamation",
    outs = [
        "php-upb.c",
        "php-upb.h",
    ],
    libs = [
        ":upb",
        ":fastdecode",
        ":descriptor_upb_proto",
        ":descriptor_upb_proto_reflection",
        ":reflection",
        ":port",
        ":json",
    ],
    prefix = "php-",
)

cc_library(
    name = "php_amalgamation",
    srcs = ["php-upb.c"],
    hdrs = ["php-upb.h"],
    copts = UPB_DEFAULT_COPTS,
    deps = ["//third_party/utf8_range"],
)

upb_amalgamation(
    name = "gen_ruby_amalgamation",
    outs = [
        "ruby-upb.c",
        "ruby-upb.h",
    ],
    libs = [
        ":upb",
        ":fastdecode",
        ":descriptor_upb_proto",
        ":reflection",
        ":port",
        ":json",
    ],
    prefix = "ruby-",
)

cc_library(
    name = "ruby_amalgamation",
    srcs = ["ruby-upb.c"],
    hdrs = ["ruby-upb.h"],
    copts = UPB_DEFAULT_COPTS,
    deps = ["//third_party/utf8_range"],
)

exports_files(
    [
        "third_party/lunit/console.lua",
        "third_party/lunit/lunit.lua",
    ],
    visibility = ["//upb/bindings/lua:__pkg__"],
)

filegroup(
    name = "cmake_files",
    srcs = glob([
        "google/**/*",
        "upbc/**/*",
        "upb/**/*",
        "tests/**/*",
        "third_party/**/*",
    ]),
    visibility = ["//cmake:__pkg__"],
)

# end:github_only
