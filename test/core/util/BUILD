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

load("//bazel:custom_exec_properties.bzl", "LARGE_MACHINE")
load("//bazel:grpc_build_system.bzl", "grpc_cc_test", "grpc_package")
load(
    "//test/core/test_util:grpc_fuzzer.bzl",
    "grpc_fuzzer",
    "grpc_proto_fuzzer",
)

licenses(["notice"])

grpc_package(name = "test/core/util")

grpc_cc_test(
    name = "directory_reader_test",
    srcs = ["directory_reader_test.cc"],
    data = [
        "//test/core/tsi/test_creds/crl_data/crls:ab06acdd.r0",
        "//test/core/tsi/test_creds/crl_data/crls:b9322cac.r0",
        "//test/core/tsi/test_creds/crl_data/crls:current.crl",
        "//test/core/tsi/test_creds/crl_data/crls:intermediate.crl",
    ],
    external_deps = [
        "gtest",
    ],
    language = "C++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//src/core:directory_reader",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "examine_stack_test",
    srcs = ["examine_stack_test.cc"],
    external_deps = [
        "absl/debugging:stacktrace",
        "absl/debugging:symbolize",
        "absl/log:log",
        "gtest",
    ],
    language = "C++",
    # TODO(https://github.com/grpc/grpc/issues/24627): Disable this on Windows
    tags = ["no_windows"],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:gpr",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "dump_args_test",
    srcs = ["dump_args_test.cc"],
    external_deps = [
        "gtest",
    ],
    language = "C++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//src/core:dump_args",
    ],
)

# TODO(hork): solidify fork support requirements for EventEngines
grpc_cc_test(
    name = "fork_test",
    srcs = ["fork_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    tags = ["no_windows"],
    uses_event_engine = True,  # engines should behave appropriately on Fork
    uses_polling = False,
    deps = [
        "//:gpr",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "bitset_test",
    srcs = ["bitset_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//src/core:bitset",
    ],
)

grpc_cc_test(
    name = "if_list_test",
    srcs = ["if_list_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//src/core:if_list",
    ],
)

grpc_cc_test(
    name = "no_destruct_test",
    srcs = ["no_destruct_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//src/core:no_destruct",
    ],
)

grpc_cc_test(
    name = "match_test",
    srcs = ["match_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//src/core:match",
    ],
)

grpc_cc_test(
    name = "overload_test",
    srcs = ["overload_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//src/core:overload",
    ],
)

grpc_cc_test(
    name = "down_cast_test",
    srcs = ["down_cast_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//src/core:down_cast",
    ],
)

grpc_cc_test(
    name = "table_test",
    srcs = ["table_test.cc"],
    external_deps = [
        "gtest",
        "absl/types:optional",
    ],
    language = "C++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//src/core:table",
    ],
)

grpc_cc_test(
    name = "host_port_test",
    srcs = ["host_port_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:gpr",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "mpscq_test",
    srcs = ["mpscq_test.cc"],
    exec_properties = LARGE_MACHINE,
    external_deps = [
        "absl/log:log",
        "gtest",
    ],
    language = "C++",
    tags = ["no_windows"],  # LARGE_MACHINE is not configured for windows RBE
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:gpr",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "orphanable_test",
    srcs = ["orphanable_test.cc"],
    external_deps = [
        "gtest",
    ],
    language = "C++",
    deps = [
        "//:orphanable",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "ref_counted_test",
    srcs = ["ref_counted_test.cc"],
    external_deps = [
        "gtest",
    ],
    language = "C++",
    deps = [
        "//src/core:ref_counted",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "dual_ref_counted_test",
    srcs = ["dual_ref_counted_test.cc"],
    external_deps = [
        "absl/log:check",
        "gtest",
    ],
    language = "C++",
    deps = [
        "//src/core:dual_ref_counted",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "ref_counted_ptr_test",
    srcs = ["ref_counted_ptr_test.cc"],
    external_deps = [
        "absl/container:flat_hash_set",
        "absl/log:check",
        "gtest",
    ],
    language = "C++",
    deps = [
        "//:ref_counted_ptr",
        "//src/core:dual_ref_counted",
        "//src/core:ref_counted",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "thd_test",
    srcs = ["thd_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:gpr",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "stat_test",
    srcs = ["stat_test.cc"],
    external_deps = [
        "gtest",
    ],
    language = "C++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "status_helper_test",
    srcs = ["status_helper_test.cc"],
    external_deps = [
        "gtest",
    ],
    language = "C++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:gpr",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "time_util_test",
    srcs = ["time_util_test.cc"],
    external_deps = [
        "gtest",
    ],
    language = "C++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:gpr",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "cpp_impl_of_test",
    srcs = ["cpp_impl_of_test.cc"],
    external_deps = ["gtest"],
    language = "c++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:cpp_impl_of",
    ],
)

grpc_cc_test(
    name = "chunked_vector_test",
    srcs = ["chunked_vector_test.cc"],
    external_deps = ["gtest"],
    language = "c++",
    tags = [
        "resource_quota_test",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:gpr",
        "//src/core:chunked_vector",
        "//src/core:resource_quota",
    ],
)

grpc_proto_fuzzer(
    name = "chunked_vector_fuzzer",
    srcs = ["chunked_vector_fuzzer.cc"],
    corpus = "chunked_vector_corpora",
    external_deps = ["absl/log:check"],
    language = "C++",
    proto = "chunked_vector_fuzzer.proto",
    tags = ["no_windows"],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//src/core:chunked_vector",
        "//src/core:resource_quota",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "time_test",
    srcs = ["time_test.cc"],
    external_deps = ["gtest"],
    language = "c++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//src/core:time",
    ],
)

grpc_cc_test(
    name = "single_set_ptr_test",
    srcs = ["single_set_ptr_test.cc"],
    external_deps = [
        "absl/log:log",
        "gtest",
    ],
    language = "c++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//src/core:single_set_ptr",
    ],
)

grpc_cc_test(
    name = "sorted_pack_test",
    srcs = ["sorted_pack_test.cc"],
    external_deps = ["gtest"],
    language = "c++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//src/core:sorted_pack",
    ],
)

grpc_cc_test(
    name = "unique_type_name_test",
    srcs = ["unique_type_name_test.cc"],
    external_deps = [
        "gtest",
        "absl/container:flat_hash_map",
        "absl/strings:str_format",
    ],
    language = "c++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//src/core:unique_type_name",
    ],
)

grpc_cc_test(
    name = "work_serializer_test",
    srcs = ["work_serializer_test.cc"],
    exec_properties = LARGE_MACHINE,
    external_deps = [
        "gtest",
    ],
    flaky = True,
    language = "C++",
    shard_count = 5,
    tags = [
        "no_windows",  # LARGE_MACHINE is not configured for windows RBE
    ],
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/event_engine:event_engine_test_utils",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "validation_errors_test",
    srcs = ["validation_errors_test.cc"],
    external_deps = [
        "gtest",
    ],
    language = "C++",
    deps = [
        "//src/core:validation_errors",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "notification_test",
    srcs = ["notification_test.cc"],
    external_deps = [
        "gtest",
    ],
    language = "C++",
    uses_event_engine = False,
    uses_polling = False,
    deps = ["//src/core:notification"],
)

grpc_cc_test(
    name = "load_file_test",
    srcs = ["load_file_test.cc"],
    external_deps = [
        "gtest",
    ],
    language = "C++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//src/core:load_file",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "uuid_v4_test",
    srcs = ["uuid_v4_test.cc"],
    external_deps = [
        "gtest",
    ],
    language = "C++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//src/core:uuid_v4",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "glob_test",
    srcs = ["glob_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:gpr",
        "//src/core:useful",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_fuzzer(
    name = "uri_fuzzer_test",
    srcs = ["uri_fuzzer_test.cc"],
    corpus = "uri_corpus",
    language = "C++",
    tags = [
        "no_windows",
        # Without "nofixdeps", "//:grpc" gets substituted with "//:event_engine_base_hdrs"
        "nofixdeps",
    ],
    deps = [
        "//:exec_ctx",
        "//:gpr",
        "//:grpc",
        "//:uri",
    ],
)

grpc_cc_test(
    name = "uri_test",
    srcs = ["uri_test.cc"],
    external_deps = [
        "absl/status",
        "gtest",
    ],
    language = "C++",
    deps = [
        "//:event_engine_base_hdrs",
        "//:uri",
        "//test/core/test_util:grpc_test_util_unsecure",
    ],
)

grpc_cc_test(
    name = "matchers_test",
    srcs = ["matchers_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
    ],
)

grpc_cc_test(
    name = "backoff_test",
    srcs = ["backoff_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:backoff",
        "//:exec_ctx",
        "//:grpc",
        "//src/core:time",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "random_early_detection_test",
    srcs = ["random_early_detection_test.cc"],
    external_deps = [
        "absl/random",
        "gtest",
    ],
    language = "C++",
    uses_event_engine = False,
    uses_polling = False,
    deps = ["//src/core:random_early_detection"],
)

grpc_cc_test(
    name = "alloc_test",
    srcs = ["alloc_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:gpr",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "cpu_test",
    srcs = ["cpu_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:gpr",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "env_test",
    srcs = ["env_test.cc"],
    external_deps = [
        "absl/log:log",
        "gtest",
    ],
    language = "C++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:gpr",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "spinlock_test",
    srcs = ["spinlock_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:gpr",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "string_test",
    srcs = ["string_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:gpr",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "sync_test",
    srcs = ["sync_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:gpr",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "gpr_time_test",
    srcs = ["gpr_time_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:gpr",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "unique_ptr_with_bitset_test",
    srcs = ["unique_ptr_with_bitset_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:gpr_platform",
        "//src/core:unique_ptr_with_bitset",
    ],
)

grpc_cc_test(
    name = "useful_test",
    srcs = ["useful_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:gpr_platform",
        "//src/core:useful",
    ],
)

grpc_cc_test(
    name = "ring_buffer_test",
    srcs = ["ring_buffer_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:gpr_platform",
        "//src/core:ring_buffer",
    ],
)

grpc_cc_test(
    name = "avl_test",
    srcs = ["avl_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//src/core:avl",
    ],
)

grpc_proto_fuzzer(
    name = "avl_fuzzer",
    srcs = ["avl_fuzzer.cc"],
    corpus = "avl_fuzzer_corpus",
    language = "C++",
    proto = "avl_fuzzer.proto",
    tags = ["no_windows"],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//src/core:avl",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "lru_cache_test",
    srcs = ["lru_cache_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//src/core:lru_cache",
    ],
)
