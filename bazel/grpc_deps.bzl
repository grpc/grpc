"""Load dependencies needed to compile and test the grpc library as a 3rd-party consumer."""

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")
load("@com_github_grpc_grpc//bazel:grpc_python_deps.bzl", "grpc_python_deps")

def grpc_deps():
    """Loads dependencies need to compile and test the grpc library."""

    native.bind(
        name = "upb_lib",
        actual = "@upb//:upb",
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
        name = "madler_zlib",
        actual = "@zlib//:zlib",
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
        name = "benchmark",
        actual = "@com_github_google_benchmark//:benchmark",
    )

    native.bind(
        name = "gflags",
        actual = "@com_github_gflags_gflags//:gflags",
    )

    native.bind(
        name = "grpc_cpp_plugin",
        actual = "@com_github_grpc_grpc//src/compiler:grpc_cpp_plugin",
    )

    native.bind(
        name = "grpc++_codegen_proto",
        actual = "@com_github_grpc_grpc//:grpc++_codegen_proto",
    )

    native.bind(
        name = "opencensus-trace",
        actual = "@io_opencensus_cpp//opencensus/trace:trace",
    )

    native.bind(
        name = "opencensus-stats",
        actual = "@io_opencensus_cpp//opencensus/stats:stats",
    )

    native.bind(
        name = "opencensus-stats-test",
        actual = "@io_opencensus_cpp//opencensus/stats:test_utils",
    )

    if "boringssl" not in native.existing_rules():
        http_archive(
            name = "boringssl",
            # NOTE: This URL generates a tarball containing dynamic date
            # information, so the sha256 is not consistent.
            url = "https://boringssl.googlesource.com/boringssl/+archive/83da28a68f32023fd3b95a8ae94991a07b1f6c62.tar.gz",
        )

    if "zlib" not in native.existing_rules():
        http_archive(
            name = "zlib",
            build_file = "@com_github_grpc_grpc//third_party:zlib.BUILD",
            sha256 = "6d4d6640ca3121620995ee255945161821218752b551a1a180f4215f7d124d45",
            strip_prefix = "zlib-cacf7f1d4e3d44d871b605da3b647f07d718623f",
            url = "https://github.com/madler/zlib/archive/cacf7f1d4e3d44d871b605da3b647f07d718623f.tar.gz",
        )

    if "com_google_protobuf" not in native.existing_rules():
        http_archive(
            name = "com_google_protobuf",
            sha256 = "416212e14481cff8fd4849b1c1c1200a7f34808a54377e22d7447efdf54ad758",
            strip_prefix = "protobuf-09745575a923640154bcf307fba8aedff47f240a",
            url = "https://github.com/google/protobuf/archive/09745575a923640154bcf307fba8aedff47f240a.tar.gz",
        )

    if "com_github_google_googletest" not in native.existing_rules():
        http_archive(
            name = "com_github_google_googletest",
            sha256 = "443d383db648ebb8e391382c0ab63263b7091d03197f304390baac10f178a468",
            strip_prefix = "googletest-c9ccac7cb7345901884aabf5d1a786cfa6e2f397",
            url = "https://github.com/google/googletest/archive/c9ccac7cb7345901884aabf5d1a786cfa6e2f397.tar.gz",  # 2019-08-19
        )

    if "rules_cc" not in native.existing_rules():
        http_archive(
            name = "rules_cc",
            sha256 = "35f2fb4ea0b3e61ad64a369de284e4fbbdcdba71836a5555abb5e194cf119509",
            strip_prefix = "rules_cc-624b5d59dfb45672d4239422fa1e3de1822ee110",
            url = "https://github.com/bazelbuild/rules_cc/archive/624b5d59dfb45672d4239422fa1e3de1822ee110.tar.gz",  #2019-08-15
        )

    if "com_github_gflags_gflags" not in native.existing_rules():
        http_archive(
            name = "com_github_gflags_gflags",
            sha256 = "63ae70ea3e05780f7547d03503a53de3a7d2d83ad1caaa443a31cb20aea28654",
            strip_prefix = "gflags-28f50e0fed19872e0fd50dd23ce2ee8cd759338e",
            url = "https://github.com/gflags/gflags/archive/28f50e0fed19872e0fd50dd23ce2ee8cd759338e.tar.gz",
        )

    if "com_github_google_benchmark" not in native.existing_rules():
        http_archive(
            name = "com_github_google_benchmark",
            sha256 = "f68aec93154d010324c05bcd8c5cc53468b87af88d87acb5ddcfaa1bba044837",
            strip_prefix = "benchmark-090faecb454fbd6e6e17a75ef8146acb037118d4",
            url = "https://github.com/google/benchmark/archive/090faecb454fbd6e6e17a75ef8146acb037118d4.tar.gz",
        )

    if "com_github_cares_cares" not in native.existing_rules():
        http_archive(
            name = "com_github_cares_cares",
            build_file = "@com_github_grpc_grpc//third_party:cares/cares.BUILD",
            sha256 = "e8c2751ddc70fed9dc6f999acd92e232d5846f009ee1674f8aee81f19b2b915a",
            strip_prefix = "c-ares-e982924acee7f7313b4baa4ee5ec000c5e373c30",
            url = "https://github.com/c-ares/c-ares/archive/e982924acee7f7313b4baa4ee5ec000c5e373c30.tar.gz",
        )

    if "com_google_absl" not in native.existing_rules():
        http_archive(
            name = "com_google_absl",
            sha256 = "fd4edc10767c28b23bf9f41114c6bcd9625c165a31baa0e6939f01058029a912",
            strip_prefix = "abseil-cpp-74d91756c11bc22f9b0108b94da9326f7f9e376f",
            url = "https://github.com/abseil/abseil-cpp/archive/74d91756c11bc22f9b0108b94da9326f7f9e376f.tar.gz",
        )

    if "bazel_toolchains" not in native.existing_rules():
        # list of releases is at https://releases.bazel.build/bazel-toolchains.html
        http_archive(
            name = "bazel_toolchains",
            sha256 = "e9bab54199722935f239cb1cd56a80be2ac3c3843e1a6d3492e2bc11f9c21daf",
            strip_prefix = "bazel-toolchains-1.0.0",
            urls = [
                "https://github.com/bazelbuild/bazel-toolchains/releases/download/1.0.0/bazel-toolchains-1.0.0.tar.gz",
                "https://mirror.bazel.build/github.com/bazelbuild/bazel-toolchains/archive/1.0.0.tar.gz",
            ],
        )

    if "bazel_skylib" not in native.existing_rules():
        http_archive(
            name = "bazel_skylib",
            urls = [
                "https://mirror.bazel.build/github.com/bazelbuild/bazel-skylib/releases/download/1.0.2/bazel-skylib-1.0.2.tar.gz",
                "https://github.com/bazelbuild/bazel-skylib/releases/download/1.0.2/bazel-skylib-1.0.2.tar.gz",
            ],
            sha256 = "97e70364e9249702246c0e9444bccdc4b847bed1eb03c5a3ece4f83dfe6abc44",
        )

    if "io_opencensus_cpp" not in native.existing_rules():
        http_archive(
            name = "io_opencensus_cpp",
            sha256 = "90d6fafa8b1a2ea613bf662731d3086e1c2ed286f458a95c81744df2dbae41b1",
            strip_prefix = "opencensus-cpp-c9a4da319bc669a772928ffc55af4a61be1a1176",
            url = "https://github.com/census-instrumentation/opencensus-cpp/archive/c9a4da319bc669a772928ffc55af4a61be1a1176.tar.gz",
        )
    if "upb" not in native.existing_rules():
        http_archive(
            name = "upb",
            sha256 = "61d0417abd60e65ed589c9deee7c124fe76a4106831f6ad39464e1525cef1454",
            strip_prefix = "upb-9effcbcb27f0a665f9f345030188c0b291e32482",
            url = "https://github.com/protocolbuffers/upb/archive/9effcbcb27f0a665f9f345030188c0b291e32482.tar.gz",
        )
    if "envoy_api" not in native.existing_rules():
        http_archive(
            name = "envoy_api",
            sha256 = "9e8cf42abce32c9b0e9e271b0cb62803084cbe5e5b49f5d5c2aef0766f9d69ca",
            strip_prefix = "data-plane-api-c83ed7ea9eb5fb3b93d1ad52b59750f1958b8bde",
            url = "https://github.com/envoyproxy/data-plane-api/archive/c83ed7ea9eb5fb3b93d1ad52b59750f1958b8bde.tar.gz",
        )

    if "io_bazel_rules_go" not in native.existing_rules():
        http_archive(
            name = "io_bazel_rules_go",
            urls = ["https://github.com/bazelbuild/rules_go/releases/download/0.18.5/rules_go-0.18.5.tar.gz"],
            sha256 = "a82a352bffae6bee4e95f68a8d80a70e87f42c4741e6a448bec11998fcc82329",
        )

    if "build_bazel_rules_apple" not in native.existing_rules():
        http_archive(
            name = "build_bazel_rules_apple",
            url = "https://github.com/bazelbuild/rules_apple/archive/b869b0d3868d78a1d4ffd866ccb304fb68aa12c3.tar.gz",
            strip_prefix = "rules_apple-b869b0d3868d78a1d4ffd866ccb304fb68aa12c3",
            sha256 = "bdc8e66e70b8a75da23b79f1f8c6207356df07d041d96d2189add7ee0780cf4e",
        )

    if "build_bazel_apple_support" not in native.existing_rules():
        http_archive(
            name = "build_bazel_apple_support",
            urls = [
                "https://github.com/bazelbuild/apple_support/releases/download/0.7.1/apple_support.0.7.1.tar.gz",
            ],
            sha256 = "122ebf7fe7d1c8e938af6aeaee0efe788a3a2449ece5a8d6a428cb18d6f88033",
        )

    grpc_python_deps()

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
        http_archive(
            name = "com_github_twisted_twisted",
            sha256 = "ca17699d0d62eafc5c28daf2c7d0a18e62ae77b4137300b6c7d7868b39b06139",
            strip_prefix = "twisted-twisted-17.5.0",
            url = "https://github.com/twisted/twisted/archive/twisted-17.5.0.zip",
            build_file = "@com_github_grpc_grpc//third_party:twisted.BUILD",
        )

    if "com_github_yaml_pyyaml" not in native.existing_rules():
        http_archive(
            name = "com_github_yaml_pyyaml",
            sha256 = "6b4314b1b2051ddb9d4fcd1634e1fa9c1bb4012954273c9ff3ef689f6ec6c93e",
            strip_prefix = "pyyaml-3.12",
            url = "https://github.com/yaml/pyyaml/archive/3.12.zip",
            build_file = "@com_github_grpc_grpc//third_party:yaml.BUILD",
        )

    if "com_github_twisted_incremental" not in native.existing_rules():
        http_archive(
            name = "com_github_twisted_incremental",
            sha256 = "f0ca93359ee70243ff7fbf2d904a6291810bd88cb80ed4aca6fa77f318a41a36",
            strip_prefix = "incremental-incremental-17.5.0",
            url = "https://github.com/twisted/incremental/archive/incremental-17.5.0.zip",
            build_file = "@com_github_grpc_grpc//third_party:incremental.BUILD",
        )

    if "com_github_zopefoundation_zope_interface" not in native.existing_rules():
        http_archive(
            name = "com_github_zopefoundation_zope_interface",
            sha256 = "e9579fc6149294339897be3aa9ecd8a29217c0b013fe6f44fcdae00e3204198a",
            strip_prefix = "zope.interface-4.4.3",
            url = "https://github.com/zopefoundation/zope.interface/archive/4.4.3.zip",
            build_file = "@com_github_grpc_grpc//third_party:zope_interface.BUILD",
        )

    if "com_github_twisted_constantly" not in native.existing_rules():
        http_archive(
            name = "com_github_twisted_constantly",
            sha256 = "2702cd322161a579d2c0dbf94af4e57712eedc7bd7bbbdc554a230544f7d346c",
            strip_prefix = "constantly-15.1.0",
            url = "https://github.com/twisted/constantly/archive/15.1.0.zip",
            build_file = "@com_github_grpc_grpc//third_party:constantly.BUILD",
        )
