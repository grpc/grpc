"""Load dependencies needed to compile and test the grpc library as a 3rd-party consumer."""

def grpc_deps():
    """Loads dependencies need to compile and test the grpc library."""

    native.bind(
        name = "nanopb",
        actual = "@com_github_nanopb_nanopb//:nanopb",
    )

    native.bind(
        name = "absl-base",
        actual = "@com_google_absl//absl/base",
    )

    native.bind(
        name = "absl-time",
        actual = "@com_google_absl//absl/time:time",
    )

    native.bind(
        name = "libssl",
        actual = "@boringssl//:ssl",
    )

    native.bind(
        name = "zlib",
        actual = "@com_github_madler_zlib//:z",
    )

    native.bind(
        name = "protobuf",
        actual = "@com_google_protobuf//:protobuf",
    )

    native.bind(
        name = "protobuf_clib",
        actual = "@com_google_protobuf//:protoc_lib",
    )

    native.bind(
        name = "protobuf_headers",
        actual = "@com_google_protobuf//:protobuf_headers",
    )

    native.bind(
        name = "protocol_compiler",
        actual = "@com_google_protobuf//:protoc",
    )

    native.bind(
        name = "cares",
        actual = "@com_github_cares_cares//:ares",
    )

    native.bind(
        name = "gtest",
        actual = "@com_github_google_googletest//:gtest",
    )

    native.bind(
        name = "gmock",
        actual = "@com_github_google_googletest//:gmock",
    )

    native.bind(
        name = "benchmark",
        actual = "@com_github_google_benchmark//:benchmark",
    )

    native.bind(
        name = "gflags",
        actual = "@com_github_gflags_gflags//:gflags",
    )

    native.bind(
        name = "grpc_cpp_plugin",
        actual = "@com_github_grpc_grpc//:grpc_cpp_plugin",
    )

    native.bind(
        name = "grpc++_codegen_proto",
        actual = "@com_github_grpc_grpc//:grpc++_codegen_proto",
    )

    native.bind(
        name = "opencensus-trace",
        actual = "@io_opencensus_cpp//opencensus/trace:trace"
    )

    native.bind(
        name = "opencensus-stats",
        actual = "@io_opencensus_cpp//opencensus/stats:stats"
    )

    native.bind(
        name = "opencensus-stats-test",
        actual = "@io_opencensus_cpp//opencensus/stats:test_utils"
    )

    if "boringssl" not in native.existing_rules():
        native.http_archive(
            name = "boringssl",
            # on the chromium-stable-with-bazel branch
            url = "https://boringssl.googlesource.com/boringssl/+archive/dcd3e6e6ecddf059adb48fca45bc7346a108bdd9.tar.gz",
        )

    if "com_github_madler_zlib" not in native.existing_rules():
        native.new_http_archive(
            name = "com_github_madler_zlib",
            build_file = "@com_github_grpc_grpc//third_party:zlib.BUILD",
            strip_prefix = "zlib-cacf7f1d4e3d44d871b605da3b647f07d718623f",
            url = "https://github.com/madler/zlib/archive/cacf7f1d4e3d44d871b605da3b647f07d718623f.tar.gz",
        )

    if "com_google_protobuf" not in native.existing_rules():
        native.http_archive(
            name = "com_google_protobuf",
            strip_prefix = "protobuf-48cb18e5c419ddd23d9badcfe4e9df7bde1979b2",
            url = "https://github.com/google/protobuf/archive/48cb18e5c419ddd23d9badcfe4e9df7bde1979b2.tar.gz",
        )

    if "com_github_nanopb_nanopb" not in native.existing_rules():
        native.new_http_archive(
            name = "com_github_nanopb_nanopb",
            build_file = "@com_github_grpc_grpc//third_party:nanopb.BUILD",
            strip_prefix = "nanopb-f8ac463766281625ad710900479130c7fcb4d63b",
            url = "https://github.com/nanopb/nanopb/archive/f8ac463766281625ad710900479130c7fcb4d63b.tar.gz",
        )

    if "com_github_google_googletest" not in native.existing_rules():
        native.new_http_archive(
            name = "com_github_google_googletest",
            build_file = "@com_github_grpc_grpc//third_party:gtest.BUILD",
            strip_prefix = "googletest-ec44c6c1675c25b9827aacd08c02433cccde7780",
            url = "https://github.com/google/googletest/archive/ec44c6c1675c25b9827aacd08c02433cccde7780.tar.gz",
        )

    if "com_github_gflags_gflags" not in native.existing_rules():
        native.http_archive(
            name = "com_github_gflags_gflags",
            strip_prefix = "gflags-30dbc81fb5ffdc98ea9b14b1918bfe4e8779b26e",
            url = "https://github.com/gflags/gflags/archive/30dbc81fb5ffdc98ea9b14b1918bfe4e8779b26e.tar.gz",
        )

    if "com_github_google_benchmark" not in native.existing_rules():
        native.new_http_archive(
            name = "com_github_google_benchmark",
            build_file = "@com_github_grpc_grpc//third_party:benchmark.BUILD",
            strip_prefix = "benchmark-9913418d323e64a0111ca0da81388260c2bbe1e9",
            url = "https://github.com/google/benchmark/archive/9913418d323e64a0111ca0da81388260c2bbe1e9.tar.gz",
        )

    if "com_github_cares_cares" not in native.existing_rules():
        native.new_http_archive(
            name = "com_github_cares_cares",
            build_file = "@com_github_grpc_grpc//third_party:cares/cares.BUILD",
            strip_prefix = "c-ares-3be1924221e1326df520f8498d704a5c4c8d0cce",
            url = "https://github.com/c-ares/c-ares/archive/3be1924221e1326df520f8498d704a5c4c8d0cce.tar.gz",
        )

    if "com_google_absl" not in native.existing_rules():
        native.http_archive(
            name = "com_google_absl",
            strip_prefix = "abseil-cpp-cd95e71df6eaf8f2a282b1da556c2cf1c9b09207",
            url = "https://github.com/abseil/abseil-cpp/archive/cd95e71df6eaf8f2a282b1da556c2cf1c9b09207.tar.gz",
        )

    if "com_github_bazelbuild_bazeltoolchains" not in native.existing_rules():
        native.http_archive(
            name = "com_github_bazelbuild_bazeltoolchains",
            strip_prefix = "bazel-toolchains-4653c01284d8a4a536f8f9bb47b7d10f94c549e7",
            urls = [
                "https://mirror.bazel.build/github.com/bazelbuild/bazel-toolchains/archive/4653c01284d8a4a536f8f9bb47b7d10f94c549e7.tar.gz",
                "https://github.com/bazelbuild/bazel-toolchains/archive/4653c01284d8a4a536f8f9bb47b7d10f94c549e7.tar.gz",
            ],
            sha256 = "1c4a532b396c698e6467a1548554571cb85fa091e472b05e398ebc836c315d77",
        )

    if "io_opencensus_cpp" not in native.existing_rules():
      native.http_archive(
            name = "io_opencensus_cpp",
            strip_prefix = "opencensus-cpp-fdf0f308b1631bb4a942e32ba5d22536a6170274",
            url = "https://github.com/census-instrumentation/opencensus-cpp/archive/fdf0f308b1631bb4a942e32ba5d22536a6170274.tar.gz",
        )


# TODO: move some dependencies from "grpc_deps" here?
def grpc_test_only_deps():
    """Internal, not intended for use by packages that are consuming grpc.
       Loads dependencies that are only needed to run grpc library's tests."""
    native.bind(
        name = "twisted",
        actual = "@com_github_twisted_twisted//:twisted",
    )

    native.bind(
        name = "yaml",
        actual = "@com_github_yaml_pyyaml//:yaml",
    )

    if "com_github_twisted_twisted" not in native.existing_rules():
        native.new_http_archive(
            name = "com_github_twisted_twisted",
            strip_prefix = "twisted-twisted-17.5.0",
            url = "https://github.com/twisted/twisted/archive/twisted-17.5.0.zip",
            build_file = "@com_github_grpc_grpc//third_party:twisted.BUILD",
        )

    if "com_github_yaml_pyyaml" not in native.existing_rules():
        native.new_http_archive(
            name = "com_github_yaml_pyyaml",
            strip_prefix = "pyyaml-3.12",
            url = "https://github.com/yaml/pyyaml/archive/3.12.zip",
            build_file = "@com_github_grpc_grpc//third_party:yaml.BUILD",
        )

    if "com_github_twisted_incremental" not in native.existing_rules():
        native.new_http_archive(
            name = "com_github_twisted_incremental",
            strip_prefix = "incremental-incremental-17.5.0",
            url = "https://github.com/twisted/incremental/archive/incremental-17.5.0.zip",
            build_file = "@com_github_grpc_grpc//third_party:incremental.BUILD",
        )

    if "com_github_zopefoundation_zope_interface" not in native.existing_rules():
        native.new_http_archive(
            name = "com_github_zopefoundation_zope_interface",
            strip_prefix = "zope.interface-4.4.3",
            url = "https://github.com/zopefoundation/zope.interface/archive/4.4.3.zip",
            build_file = "@com_github_grpc_grpc//third_party:zope_interface.BUILD",
        )

    if "com_github_twisted_constantly" not in native.existing_rules():
        native.new_http_archive(
            name = "com_github_twisted_constantly",
            strip_prefix = "constantly-15.1.0",
            url = "https://github.com/twisted/constantly/archive/15.1.0.zip",
            build_file = "@com_github_grpc_grpc//third_party:constantly.BUILD",
        )
