licenses(["notice"])
native.objc_library(
    name = "GTM_GoogleTestRunner_GTM_USING_XCTEST",
    testonly = 1,
    srcs = [
        "UnitTesting/GTMGoogleTestRunner.mm",
    ],
    copts = [
        "-DGTM_USING_XCTEST",
    ],
    deps = [
        "//external:gtest",
    ],
    visibility = ["//visibility:public"]
)
