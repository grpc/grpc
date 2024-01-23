exports_files([
    "gtest.BUILD",
    "zlib.BUILD",
    "twisted.BUILD",
    "yaml.BUILD",
    "incremental.BUILD",
    "zope_interface.BUILD",
    "constantly.BUILD",
    "cython.BUILD",
    "six.BUILD",
    "enum34.BUILD",
    "futures.BUILD",
    "protobuf.patch",
    "protoc-gen-validate.patch",
])

package(default_visibility = ["//:__subpackages__"])

alias(
    name = "upb_base_lib",
    actual = "@com_google_protobuf//upb/base",
    tags = ["manual"],
)

alias(
    name = "upb_message_lib",
    actual = "@com_google_protobuf//upb:message",
    tags = ["manual"],
)

alias(
    name = "upb_mem_lib",
    actual = "@com_google_protobuf//upb/mem",
    tags = ["manual"],
)

alias(
    name = "upb_reflection",
    actual = "@com_google_protobuf//upb:reflection",
    tags = ["manual"],
)

alias(
    name = "upb_lib_descriptor",
    actual = "@com_google_protobuf//upb:descriptor_upb_proto",
    tags = ["manual"],
)

alias(
    name = "upb_lib_descriptor_reflection",
    actual = "@com_google_protobuf//upb:descriptor_upb_proto_reflection",
    tags = ["manual"],
)

alias(
    name = "upb_textformat_lib",
    actual = "@com_google_protobuf//upb/text",
    tags = ["manual"],
)

alias(
    name = "upb_json_lib",
    actual = "@com_google_protobuf//upb/json",
    tags = ["manual"],
)

alias(
    name = "upb_generated_code_support__only_for_generated_code_do_not_use__i_give_permission_to_break_me",
    actual = "@com_google_protobuf//upb:generated_code_support__only_for_generated_code_do_not_use__i_give_permission_to_break_me",
    tags = ["manual"],
)

alias(
    name = "libssl",
    actual = "@boringssl//:ssl",
    tags = ["manual"],
)

alias(
    name = "libcrypto",
    actual = "@boringssl//:crypto",
    tags = ["manual"],
)

alias(
    name = "madler_zlib",
    actual = "@zlib//:zlib",
    tags = ["manual"],
)

alias(
    name = "protobuf",
    actual = "@com_google_protobuf//:protobuf",
    tags = ["manual"],
)

alias(
    name = "protobuf_clib",
    actual = "@com_google_protobuf//:protoc_lib",
    tags = ["manual"],
)

alias(
    name = "protobuf_headers",
    actual = "@com_google_protobuf//:protobuf_headers",
    tags = ["manual"],
)

alias(
    name = "protocol_compiler",
    actual = "@com_google_protobuf//:protoc",
    tags = ["manual"],
    visibility = ["//visibility:public"],
)

alias(
    name = "cares",
    actual = "@com_github_cares_cares//:ares",
    tags = ["manual"],
)

alias(
    name = "gtest",
    actual = "@com_google_googletest//:gtest",
    tags = ["manual"],
)

alias(
    name = "fuzztest",
    actual = "@com_google_fuzztest//fuzztest",
    tags = ["manual"],
)

alias(
    name = "fuzztest_main",
    actual = "@com_google_fuzztest//fuzztest:fuzztest_gtest_main",
    tags = ["manual"],
)

alias(
    name = "benchmark",
    actual = "@com_github_google_benchmark//:benchmark",
    tags = ["manual"],
)

alias(
    name = "re2",
    actual = "@com_googlesource_code_re2//:re2",
    tags = ["manual"],
)

alias(
    name = "grpc_cpp_plugin",
    actual = "@com_github_grpc_grpc//src/compiler:grpc_cpp_plugin",
    tags = ["manual"],
)

alias(
    name = "grpc++_codegen_proto",
    actual = "@com_github_grpc_grpc//:grpc++_codegen_proto",
    tags = ["manual"],
)

alias(
    name = "opencensus-context",
    actual = "@io_opencensus_cpp//opencensus/context:context",
    tags = ["manual"],
)

alias(
    name = "opencensus-trace",
    actual = "@io_opencensus_cpp//opencensus/trace:trace",
    tags = ["manual"],
)

alias(
    name = "opencensus-trace-context_util",
    actual = "@io_opencensus_cpp//opencensus/trace:context_util",
    tags = ["manual"],
)

alias(
    name = "opencensus-trace-propagation",
    actual = "@io_opencensus_cpp//opencensus/trace:grpc_trace_bin",
    tags = ["manual"],
)

alias(
    name = "opencensus-trace-span_context",
    actual = "@io_opencensus_cpp//opencensus/trace:span_context",
    tags = ["manual"],
)

alias(
    name = "opencensus-stats",
    actual = "@io_opencensus_cpp//opencensus/stats:stats",
    tags = ["manual"],
)

alias(
    name = "opencensus-stats-test",
    actual = "@io_opencensus_cpp//opencensus/stats:test_utils",
    tags = ["manual"],
)

alias(
    name = "opencensus-with-tag-map",
    actual = "@io_opencensus_cpp//opencensus/tags:with_tag_map",
    tags = ["manual"],
)

alias(
    name = "opencensus-tags",
    actual = "@io_opencensus_cpp//opencensus/tags:tags",
    tags = ["manual"],
)

alias(
    name = "opencensus-tags-context_util",
    actual = "@io_opencensus_cpp//opencensus/tags:context_util",
    tags = ["manual"],
)

alias(
    name = "opencensus-trace-stackdriver_exporter",
    actual = "@io_opencensus_cpp//opencensus/exporters/trace/stackdriver:stackdriver_exporter",
    tags = ["manual"],
)

alias(
    name = "opencensus-stats-stackdriver_exporter",
    actual = "@io_opencensus_cpp//opencensus/exporters/stats/stackdriver:stackdriver_exporter",
    tags = ["manual"],
)

alias(
    name = "googleapis_trace_grpc_service",
    actual = "@com_google_googleapis//google/devtools/cloudtrace/v2:cloudtrace_cc_grpc",
    tags = ["manual"],
)

alias(
    name = "googleapis_monitoring_grpc_service",
    actual = "@com_google_googleapis//google/monitoring/v3:monitoring_cc_grpc",
    tags = ["manual"],
)

alias(
    name = "googleapis_logging_grpc_service",
    actual = "@com_google_googleapis//google/logging/v2:logging_cc_grpc",
    tags = ["manual"],
)

alias(
    name = "googleapis_logging_cc_proto",
    actual = "@com_google_googleapis//google/logging/v2:logging_cc_proto",
    tags = ["manual"],
)

alias(
    name = "twisted",
    actual = "@com_github_twisted_twisted//:twisted",
    tags = ["manual"],
)

alias(
    name = "yaml",
    actual = "@com_github_yaml_pyyaml//:yaml",
    tags = ["manual"],
)
