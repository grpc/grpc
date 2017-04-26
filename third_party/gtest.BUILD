cc_library(
    name = "gtest",
    srcs = [
        "googletest/src/gtest-all.cc",
    ],
    hdrs = glob(["googletest/include/**/*.h", "googletest/src/*.cc", "googletest/src/*.h"]),
    includes = [
        "googletest",
        "googletest/include",
    ],
    linkstatic = 1,
    visibility = [
        "//visibility:public",
    ],
)
