cc_library(
    name = "gtest",
    srcs = [
        "googletest/src/gtest-all.cc",
	"googlemock/src/gmock-all.cc"
    ],
    hdrs = glob(["googletest/include/**/*.h", "googletest/src/*.cc", "googletest/src/*.h", "googlemock/include/**/*.h", "googlemock/src/*.cc", "googlemock/src/*.h"]),
    includes = [
        "googletest",
        "googletest/include",
	"googlemock",
	"googlemock/include",
    ],
    linkstatic = 1,
    visibility = [
        "//visibility:public",
    ],
)
