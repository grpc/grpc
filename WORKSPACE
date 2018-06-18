workspace(name = "com_github_grpc_grpc")

http_archive(
    name = "io_opencensus_cpp",
    strip_prefix = "opencensus-cpp-master",
    urls = ["https://github.com/census-instrumentation/opencensus-cpp/archive/master.zip"],
)

# CCTZ (Time-zone framework). Used by absl.
http_archive(
    name = "com_googlesource_code_cctz",
    urls = ["https://github.com/google/cctz/archive/master.zip"],
    strip_prefix = "cctz-master",
)

load("//bazel:grpc_deps.bzl", "grpc_deps", "grpc_test_only_deps")
grpc_deps()
grpc_test_only_deps()
