"""Load dependencies needed to compile and test the grpc library as a 3rd-party consumer."""

load("@bazel_tools//tools/build_defs/repo:git.bzl", "git_repository")
load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

def grpc_deps():
    """Loads dependencies need to compile and test the grpc library."""

    native.bind(
        name = "nanopb",
        actual = "@com_github_nanopb_nanopb//:nanopb",
    )

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
            # on the chromium-stable-with-bazel branch
            # NOTE: This URL generates a tarball containing dynamic date
            # information, so the sha256 is not consistent.
            url = "https://boringssl.googlesource.com/boringssl/+archive/afc30d43eef92979b05776ec0963c9cede5fb80f.tar.gz",
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

    if "com_github_nanopb_nanopb" not in native.existing_rules():
        http_archive(
            name = "com_github_nanopb_nanopb",
            build_file = "@com_github_grpc_grpc//third_party:nanopb.BUILD",
            sha256 = "8bbbb1e78d4ddb0a1919276924ab10d11b631df48b657d960e0c795a25515735",
            strip_prefix = "nanopb-f8ac463766281625ad710900479130c7fcb4d63b",
            url = "https://github.com/nanopb/nanopb/archive/f8ac463766281625ad710900479130c7fcb4d63b.tar.gz",
        )

    if "com_github_google_googletest" not in native.existing_rules():
        http_archive(
            name = "com_github_google_googletest",
            build_file = "@com_github_grpc_grpc//third_party:gtest.BUILD",
            sha256 = "175a22300b3450e27e5f2e6f95cc9abca74617cbc21a1e0ed19bdfbd22ea0305",
            strip_prefix = "googletest-ec44c6c1675c25b9827aacd08c02433cccde7780",
            url = "https://github.com/google/googletest/archive/ec44c6c1675c25b9827aacd08c02433cccde7780.tar.gz",
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
            sha256 = "c7682e9007ddfd94072647abab3e89ffd9084089460ae47d67060974467b58bf",
            strip_prefix = "benchmark-e776aa0275e293707b6a0901e0e8d8a8a3679508",
            url = "https://github.com/google/benchmark/archive/e776aa0275e293707b6a0901e0e8d8a8a3679508.tar.gz",
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
            sha256 = "5fe2a3a8f8378e81d4d3db6541f48030e04233ccd2d6c7e9d981ed577b314ae8",
            strip_prefix = "abseil-cpp-308ce31528a7edfa39f5f6d36142278a0ae1bf45",
            url = "https://github.com/abseil/abseil-cpp/archive/308ce31528a7edfa39f5f6d36142278a0ae1bf45.tar.gz",
        )

    if "bazel_toolchains" not in native.existing_rules():
        http_archive(
            name = "bazel_toolchains",
            sha256 = "88e818f9f03628eef609c8429c210ecf265ffe46c2af095f36c7ef8b1855fef5",
            strip_prefix = "bazel-toolchains-92dd8a7a518a2fb7ba992d47c8b38299fe0be825",
            urls = [
                "https://mirror.bazel.build/github.com/bazelbuild/bazel-toolchains/archive/92dd8a7a518a2fb7ba992d47c8b38299fe0be825.tar.gz",
                "https://github.com/bazelbuild/bazel-toolchains/archive/92dd8a7a518a2fb7ba992d47c8b38299fe0be825.tar.gz",
            ],
        )

    if "bazel_skylib" not in native.existing_rules():
        http_archive(
            name = "bazel_skylib",
            sha256 = "ba5d15ca230efca96320085d8e4d58da826d1f81b444ef8afccd8b23e0799b52",
            strip_prefix = "bazel-skylib-f83cb8dd6f5658bc574ccd873e25197055265d1c",
            url = "https://github.com/bazelbuild/bazel-skylib/archive/f83cb8dd6f5658bc574ccd873e25197055265d1c.tar.gz",
        )

    if "io_opencensus_cpp" not in native.existing_rules():
        http_archive(
            name = "io_opencensus_cpp",
            sha256 = "3436ca23dc1b3345186defd0f46d64244079ba3d3234a0c5d6ef5e8d5d06c8b5",
            strip_prefix = "opencensus-cpp-9b1e354e89bf3d92aedc00af45b418ce870f3d77",
            url = "https://github.com/census-instrumentation/opencensus-cpp/archive/9b1e354e89bf3d92aedc00af45b418ce870f3d77.tar.gz",
        )

    if "upb" not in native.existing_rules():
        http_archive(
            name = "upb",
            sha256 = "0e749a8973968397f849a3b42e28ee9c85dc418c2477954c2a6a4495f323241d",
            strip_prefix = "upb-ed9faae0993704b033c594b072d65e1bf19207fa",
            url = "https://github.com/google/upb/archive/ed9faae0993704b033c594b072d65e1bf19207fa.tar.gz",
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
