"""Load dependencies needed to compile and test the grpc library as a 3rd-party consumer."""

def grpc_deps():
    """Loads dependencies need to compile and test the grpc library."""
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

    if "boringssl" not in native.existing_rules():
        native.http_archive(
            name = "boringssl",
            # on the master-with-bazel branch
            url = "https://boringssl.googlesource.com/boringssl/+archive/886e7d75368e3f4fab3f4d0d3584e4abfc557755.tar.gz",
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
            strip_prefix = "protobuf-2761122b810fe8861004ae785cc3ab39f384d342",
            url = "https://github.com/google/protobuf/archive/2761122b810fe8861004ae785cc3ab39f384d342.tar.gz",
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
            strip_prefix = "benchmark-5b7683f49e1e9223cf9927b24f6fd3d6bd82e3f8",
            url = "https://github.com/google/benchmark/archive/5b7683f49e1e9223cf9927b24f6fd3d6bd82e3f8.tar.gz",
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
            strip_prefix = "abseil-cpp-cc4bed2d74f7c8717e31f9579214ab52a9c9c610",
            url = "https://github.com/abseil/abseil-cpp/archive/cc4bed2d74f7c8717e31f9579214ab52a9c9c610.tar.gz",
        )

    if "com_github_bazelbuild_bazeltoolchains" not in native.existing_rules():
        native.http_archive(
            name = "com_github_bazelbuild_bazeltoolchains",
            strip_prefix = "bazel-toolchains-af4681c3d19f063f090222ec3d04108c4e0ca255",
            urls = [
                "https://mirror.bazel.build/github.com/bazelbuild/bazel-toolchains/archive/af4681c3d19f063f090222ec3d04108c4e0ca255.tar.gz",
                "https://github.com/bazelbuild/bazel-toolchains/archive/af4681c3d19f063f090222ec3d04108c4e0ca255.tar.gz",
            ],
            sha256 = "d58bb2d6c8603f600d522b6104d6192a65339aa26cbba9f11ff5c4b36dedb928",
        )
