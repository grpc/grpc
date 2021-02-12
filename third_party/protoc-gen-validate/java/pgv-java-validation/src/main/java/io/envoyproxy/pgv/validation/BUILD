load("@rules_java//java:defs.bzl", "java_library")

java_library(
    name = "java_harness",
    srcs = glob(["*.java"]),
    visibility = ["//tests/harness:__subpackages__"],
    deps = [
        "//java/pgv-java-stub/src/main/java/io/envoyproxy/pgv",
        "//java/pgv-java-validation/src/main/java/io/envoyproxy/pgv",
        "//tests/harness:harness_java_proto",
        "//tests/harness/cases:cases_java_proto",
        "//tests/harness/cases:java",
        "//tests/harness/cases/other_package:embed_java_proto",
        "//tests/harness/cases/other_package:java",
        "//validate:validate_java",
        "@com_google_guava//jar",
        "@com_google_protobuf//:protobuf_java",
    ],
)
