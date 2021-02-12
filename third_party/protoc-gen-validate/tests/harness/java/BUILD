load("@rules_java//java:defs.bzl", "java_binary")

java_binary(
    name = "java-harness",
    main_class = "io.envoyproxy.pgv.validation.JavaHarness",
    visibility = ["//tests/harness:__subpackages__"],
    runtime_deps = [
        "//java/pgv-java-validation/src/main/java/io/envoyproxy/pgv/validation:java_harness",
    ],
)
