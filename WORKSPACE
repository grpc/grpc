bind(
    name = "libssl",
    actual = "@submodule_boringssl//:boringssl",
)

bind(
    name = "nanopb",
    actual = "@submodule_nanopb//:nanopb",
)

bind(
    name = "zlib",
    actual = "@submodule_zlib//:z",
)

new_local_repository(
    name = "submodule_boringssl",
    path = "third_party/boringssl",
    build_file = "third_party/boringssl.BUILD",
)

new_local_repository(
    name = "submodule_nanopb",
    path = "third_party/nanopb",
    build_file = "third_party/nanopb.BUILD",
)

new_local_repository(
    name = "submodule_zlib",
    path = "third_party/zlib",
    build_file = "third_party/zlib.BUILD",
)
