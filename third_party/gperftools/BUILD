load("@rules_foreign_cc//foreign_cc:configure.bzl", "configure_make")

package(default_visibility = ["//visibility:public"])

licenses(["notice"])

# autotools packages are unusable on Windows as-is
configure_make(
    name = "gperftools_build",
    configure_options = [
        "--enable-shared=no",
        "--enable-frame-pointers",
        "--disable-libunwind",
    ] + select({
        "//:mac_x86_64": ["AR=/usr/bin/ar"],
        "//conditions:default": [],
    }),
    lib_source = "@com_github_gperftools_gperftools//:all",
    linkopts = ["-lpthread"],
    out_static_libs = select({
        "//:debug_tcmalloc": ["libtcmalloc_debug.a"],
        "//conditions:default": ["libtcmalloc_and_profiler.a"],
    }),
    tags = ["skip_on_windows"],
    targets = [
        "install-libLTLIBRARIES install-perftoolsincludeHEADERS",
    ],
)

# Workaround for https://github.com/bazelbuild/rules_foreign_cc/issues/227
cc_library(
    name = "gperftools",
    tags = ["skip_on_windows"],
    deps = [
        "gperftools_build",
    ],
)