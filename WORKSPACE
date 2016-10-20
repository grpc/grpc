bind(
    name = "nanopb",
    actual = "//third_party/nanopb",
)

bind(
    name = "grpc_cpp_plugin",
    actual = "//:grpc_cpp_plugin",
)

bind(
    name = "grpc++",
    actual = "//:grpc++",
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
    name = "protobuf_compiler",
    actual = "@submodule_protobuf//:protoc",
)

new_local_repository(
    name = "submodule_boringssl",
    path = "third_party/boringssl",
    build_file = "third_party/boringssl/BUILD",
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
