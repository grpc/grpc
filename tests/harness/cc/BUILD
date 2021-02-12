load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")

MSVC_C_OPTS = [
    "-WX",
    "-DWIN32",
    "-DWIN32_LEAN_AND_MEAN",
]

POSIX_C_OPTS = [
    "-Wall",
    "-Wextra",
    "-Werror",
    "-Wnon-virtual-dtor",
    "-Woverloaded-virtual",
    "-Wold-style-cast",
    "-std=c++14",
]

config_setting(
    name = "windows_x86_64",
    values = {"cpu": "x64_windows"},
)

cc_binary(
    name = "cc-harness",
    srcs = ["harness.cc"],
    # These ensure that we are at least compatible with what Envoy is expecting.
    copts = select({
        ":windows_x86_64": MSVC_C_OPTS,
        "//conditions:default": POSIX_C_OPTS,
    }),
    visibility = ["//visibility:public"],
    deps = [
        "//tests/harness:harness_cc_proto",
        "//tests/harness/cases:cc",
    ],
)

# Ensure that if the headers are included in multiple libraries, those libraries
# can be linked without conflicts.
cc_test(
    name = "cc_diamond_test",
    srcs = ["diamond_test.cc"],
    copts = select({
        ":windows_x86_64": MSVC_C_OPTS,
        "//conditions:default": POSIX_C_OPTS,
    }),
    linkstatic = 1,  # Forces both libraries to be linked in. DO NOT REMOVE THIS
    deps = [
        "cc_diamond_0",
        "cc_diamond_1",
    ],
)

cc_library(
    name = "cc_diamond_0",
    srcs = ["diamond_lib.cc"],
    copts = select({
        ":windows_x86_64": MSVC_C_OPTS,
        "//conditions:default": POSIX_C_OPTS,
    }),
    deps = ["//tests/harness/cases:cc"],
    alwayslink = 1,
)

cc_library(
    name = "cc_diamond_1",
    srcs = ["diamond_lib.cc"],
    copts = select({
        ":windows_x86_64": MSVC_C_OPTS,
        "//conditions:default": POSIX_C_OPTS,
    }),
    deps = ["//tests/harness/cases:cc"],
    alwayslink = 1,
)
