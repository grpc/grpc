py_library(
    name = "twisted",
    srcs = glob(["src/twisted/**/*.py"]),
    imports = [
        "src",
    ],
    visibility = [
        "//visibility:public",
    ],
    deps = [
        "@com_github_twisted_incremental//:incremental",
        "@com_github_twisted_constantly//:constantly",
        "@com_github_zopefoundation_zope_interface//:zope_interface",
    ],
)
