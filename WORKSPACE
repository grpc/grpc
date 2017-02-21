bind(
    name = "nanopb",
    actual = "//third_party/nanopb",
)

bind(
    name = "libssl",
    actual = "@submodule_boringssl//:ssl",
)

bind(
    name = "zlib",
    actual = "@submodule_zlib//:z",
)

bind(
    name = "protobuf",
    actual = "@submodule_protobuf//:protobuf",
)

bind(
    name = "protobuf_clib",
    actual = "@submodule_protobuf//:protoc_lib",
)

bind(
    name = "protocol_compiler",
    actual = "@submodule_protobuf//:protoc",
)

bind(
    name = "gtest",
    actual = "@submodule_gtest//:gtest",
)

new_local_repository(
    name = "submodule_boringssl",
    path = "third_party/boringssl-with-bazel",
    build_file = "third_party/boringssl-with-bazel/BUILD",
)

new_local_repository(
    name = "submodule_zlib",
    path = "third_party/zlib",
    build_file = "third_party/zlib.BUILD",
)

new_local_repository(
    name = "submodule_protobuf",
    path = "third_party/protobuf",
    build_file = "third_party/protobuf/BUILD",
)

new_local_repository(
    name = "submodule_gtest",
    path = "third_party/googletest",
    build_file = "third_party/gtest.BUILD",
)
