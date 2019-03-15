cc_library(
    name = "benchmark",
    srcs = glob(["src/*.cc"]),
    hdrs = glob(["include/**/*.h", "src/*.h"]),
    includes = [
        "include", "."
    ],
    copts = [
        "-DHAVE_POSIX_REGEX"
    ],
    linkstatic = 1,
    visibility = [
        "//visibility:public",
    ],
)
