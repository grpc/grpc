cc_library(
    name = "gtest",
    srcs = [
        "googletest/src/gtest-all.cc",
    ],
    hdrs = glob(["googletest/include/**/*.h", "googletest/src/*.cc", "googletest/src/*.h"]),
    includes = [
        "googletest/include",
        "googletest",
    ],
    linkstatic = 1,
    visibility = [
        "//visibility:public",
    ],
)
