cc_library(
    name = "gtest",
    srcs = [
        "googletest/src/gtest-all.cc",
    ],
    hdrs = glob([
        "googletest/include/**/*.h",
        "googletest/src/*.cc",
        "googletest/src/*.h",
    ]),
    includes = [
        "googletest",
        "googletest/include",
    ],
    linkstatic = 1,
    visibility = [
        "//visibility:public",
    ],
)

cc_library(
    name = "gmock",
    srcs = [
        "googlemock/src/gmock-all.cc"
    ],
    hdrs = glob([
        "googlemock/include/**/*.h",
        "googlemock/src/*.cc",
        "googlemock/src/*.h"
    ]),
    includes = [
        "googlemock",
        "googlemock/include",
    ],
    deps = [
        ":gtest",
    ],
    linkstatic = 1,
    visibility = [
        "//visibility:public",
    ],
)
