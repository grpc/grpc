load("@rules_java//java:defs.bzl", "java_library")

java_library(
    name = "pgv",
    srcs = glob(["*.java"]),
    visibility = ["//visibility:public"],
    deps = [
        "//external:guava",
        "@com_google_protobuf//:protobuf_java",
        "@com_google_protobuf//:protobuf_java_util",
        "@com_google_re2j//jar",
        "@org_apache_commons_validator//jar",
    ],
)
