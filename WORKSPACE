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
    name = "cares",
    actual = "@submodule_cares//:ares",
)

bind(
    name = "gtest",
    actual = "@submodule_gtest//:gtest",
)

bind(
    name = "benchmark",
    actual = "@submodule_benchmark//:benchmark",
)

bind(
    name = "gflags",
    actual = "@com_github_gflags_gflags//:gflags",
)

new_local_repository(
    name = "submodule_boringssl",
    build_file = "third_party/boringssl-with-bazel/BUILD",
    path = "third_party/boringssl-with-bazel",
)

new_local_repository(
    name = "submodule_zlib",
    build_file = "third_party/zlib.BUILD",
    path = "third_party/zlib",
)

new_local_repository(
    name = "submodule_protobuf",
    build_file = "third_party/protobuf/BUILD",
    path = "third_party/protobuf",
)

new_local_repository(
    name = "submodule_gtest",
    build_file = "third_party/gtest.BUILD",
    path = "third_party/googletest",
)

local_repository(
    name = "com_github_gflags_gflags",
    path = "third_party/gflags",
)

new_local_repository(
    name = "submodule_benchmark",
    path = "third_party/benchmark",
    build_file = "third_party/benchmark.BUILD",
)

new_local_repository(
    name = "submodule_cares",
    path = "third_party/cares",
    build_file = "third_party/cares/cares.BUILD",
)
