bind(
    name = "nanopb",
    actual = "//third_party/nanopb",
)

bind(
    name = "libssl",
    actual = "@boringssl//:ssl",
)

bind(
    name = "zlib",
    actual = "@com_github_madler_zlib//:z",
)

bind(
    name = "protobuf",
    actual = "@com_google_protobuf//:protobuf",
)

bind(
    name = "protobuf_clib",
    actual = "@com_google_protobuf//:protoc_lib",
)

bind(
    name = "protobuf_headers",
    actual = "@com_google_protobuf//:protobuf_headers",
)

bind(
    name = "protocol_compiler",
    actual = "@com_google_protobuf//:protoc",
)

bind(
    name = "cares",
    actual = "@com_github_cares_cares//:ares",
)

bind(
    name = "gtest",
    actual = "@com_github_google_googletest//:gtest",
)

bind(
    name = "gmock",
    actual = "@com_github_google_googletest//:gmock",
)

bind(
    name = "benchmark",
    actual = "@com_github_google_benchmark//:benchmark",
)

bind(
    name = "gflags",
    actual = "@com_github_gflags_gflags//:gflags",
)

http_archive(
    name = "boringssl",
    # on the master-with-bazel branch
    url = "https://boringssl.googlesource.com/boringssl/+archive/886e7d75368e3f4fab3f4d0d3584e4abfc557755.tar.gz",
)

new_http_archive(
    name = "com_github_madler_zlib",
    build_file = "third_party/zlib.BUILD",
    strip_prefix = "zlib-cacf7f1d4e3d44d871b605da3b647f07d718623f",
    url = "https://github.com/madler/zlib/archive/cacf7f1d4e3d44d871b605da3b647f07d718623f.tar.gz",
)

http_archive(
    name = "com_google_protobuf",
    strip_prefix = "protobuf-80a37e0782d2d702d52234b62dd4b9ec74fd2c95",
    url = "https://github.com/google/protobuf/archive/80a37e0782d2d702d52234b62dd4b9ec74fd2c95.tar.gz",
)

new_http_archive(
    name = "com_github_google_googletest",
    build_file = "third_party/gtest.BUILD",
    strip_prefix = "googletest-ec44c6c1675c25b9827aacd08c02433cccde7780",
    url = "https://github.com/google/googletest/archive/ec44c6c1675c25b9827aacd08c02433cccde7780.tar.gz",
)

http_archive(
    name = "com_github_gflags_gflags",
    strip_prefix = "gflags-30dbc81fb5ffdc98ea9b14b1918bfe4e8779b26e",
    url = "https://github.com/gflags/gflags/archive/30dbc81fb5ffdc98ea9b14b1918bfe4e8779b26e.tar.gz",
)

new_http_archive(
    name = "com_github_google_benchmark",
    build_file = "third_party/benchmark.BUILD",
    strip_prefix = "benchmark-5b7683f49e1e9223cf9927b24f6fd3d6bd82e3f8",
    url = "https://github.com/google/benchmark/archive/5b7683f49e1e9223cf9927b24f6fd3d6bd82e3f8.tar.gz",
)

new_local_repository(
    name = "cares_local_files",
    build_file = "third_party/cares/cares_local_files.BUILD",
    path = "third_party/cares",
)

new_http_archive(
    name = "com_github_cares_cares",
    build_file = "third_party/cares/cares.BUILD",
    strip_prefix = "c-ares-3be1924221e1326df520f8498d704a5c4c8d0cce",
    url = "https://github.com/c-ares/c-ares/archive/3be1924221e1326df520f8498d704a5c4c8d0cce.tar.gz",
)

http_archive(
    name = "com_google_absl",
    strip_prefix = "abseil-cpp-cc4bed2d74f7c8717e31f9579214ab52a9c9c610",
    url = "https://github.com/abseil/abseil-cpp/archive/cc4bed2d74f7c8717e31f9579214ab52a9c9c610.tar.gz",
)
