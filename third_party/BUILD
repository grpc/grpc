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
    "libuv.BUILD",
    "protobuf.patch",
    "rules_python.patch",
])

package(default_visibility = ["//:__subpackages__"])

alias(
    name = "upb_lib",
    actual = "@upb//:upb",
)

alias(
    name = "upb_reflection",
    actual = "@upb//:reflection",
)

alias(
    name = "upb_lib_descriptor",
    actual = "@upb//:descriptor_upb_proto",
)

alias(
    name = "upb_lib_descriptor_reflection",
    actual = "@upb//:descriptor_upb_proto_reflection",
)

alias(
    name = "upb_textformat_lib",
    actual = "@upb//:textformat",
)

alias(
    name = "upb_json_lib",
    actual = "@upb//:json",
)

alias(
    name = "upb_generated_code_support__only_for_generated_code_do_not_use__i_give_permission_to_break_me",
    actual = "@upb//:generated_code_support__only_for_generated_code_do_not_use__i_give_permission_to_break_me",
)

alias(
    name = "absl",
    actual = "@com_google_absl//absl",
)

alias(
    name = "absl-base",
    actual = "@com_google_absl//absl/base",
)

alias(
    name = "absl-time",
    actual = "@com_google_absl//absl/time:time",
)

alias(
    name = "libssl",
    actual = "@boringssl//:ssl",
)

alias(
    name = "libcrypto",
    actual = "@boringssl//:crypto",
)

alias(
    name = "madler_zlib",
    actual = "@zlib//:zlib",
)

alias(
    name = "protobuf",
    actual = "@com_google_protobuf//:protobuf",
)

alias(
    name = "protobuf_clib",
    actual = "@com_google_protobuf//:protoc_lib",
)

alias(
    name = "protobuf_headers",
    actual = "@com_google_protobuf//:protobuf_headers",
)

alias(
    name = "protocol_compiler",
    actual = "@com_google_protobuf//:protoc",
)

alias(
    name = "cares",
    actual = "@com_github_cares_cares//:ares",
)

alias(
    name = "gtest",
    actual = "@com_google_googletest//:gtest",
)

alias(
    name = "benchmark",
    actual = "@com_github_google_benchmark//:benchmark",
)

alias(
    name = "re2",
    actual = "@com_googlesource_code_re2//:re2",
)

alias(
    name = "grpc_cpp_plugin",
    actual = "@com_github_grpc_grpc//src/compiler:grpc_cpp_plugin",
)

alias(
    name = "grpc++_codegen_proto",
    actual = "@com_github_grpc_grpc//:grpc++_codegen_proto",
)

alias(
    name = "opencensus-context",
    actual = "@io_opencensus_cpp//opencensus/context:context",
)

alias(
    name = "opencensus-trace",
    actual = "@io_opencensus_cpp//opencensus/trace:trace",
)

alias(
    name = "opencensus-trace-context_util",
    actual = "@io_opencensus_cpp//opencensus/trace:context_util",
)

alias(
    name = "opencensus-trace-propagation",
    actual = "@io_opencensus_cpp//opencensus/trace:grpc_trace_bin",
)

alias(
    name = "opencensus-stats",
    actual = "@io_opencensus_cpp//opencensus/stats:stats",
)

alias(
    name = "opencensus-stats-test",
    actual = "@io_opencensus_cpp//opencensus/stats:test_utils",
)

alias(
    name = "opencensus-with-tag-map",
    actual = "@io_opencensus_cpp//opencensus/tags:with_tag_map",
)

alias(
    name = "opencensus-tags",
    actual = "@io_opencensus_cpp//opencensus/tags:tags",
)

alias(
    name = "opencensus-tags-context_util",
    actual = "@io_opencensus_cpp//opencensus/tags:context_util",
)

alias(
    name = "libuv",
    actual = "@libuv//:libuv",
)

# For test only
alias(
    name = "twisted",
    actual = "@com_github_twisted_twisted//:twisted",
)

alias(
    name = "yaml",
    actual = "@com_github_yaml_pyyaml//:yaml",
)
