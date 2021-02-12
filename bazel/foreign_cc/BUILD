load("@rules_foreign_cc//tools/build_defs:cmake.bzl", "cmake_external")

cmake_external(
    name = "zlib",
    cache_entries = {
        "CMAKE_BUILD_TYPE": "RelWithDebInfo",
    },
    lib_source = "@net_zlib//:all",
    static_libraries = select({
        "//conditions:default": ["libz.a"],
    }),
    visibility = ["//visibility:public"],
)
