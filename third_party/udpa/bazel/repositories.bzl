load(":envoy_http_archive.bzl", "udpa_http_archive")
load(":repository_locations.bzl", "REPOSITORY_LOCATIONS")

# Make all contents of an external repository accessible under a filegroup.  Used for external HTTP
# archives, e.g. cares.
BUILD_ALL_CONTENT = """filegroup(name = "all", srcs = glob(["**"]), visibility = ["//visibility:public"])"""

def udpa_api_dependencies():
    udpa_http_archive(
        "bazel_gazelle",
        locations = REPOSITORY_LOCATIONS,
    )
    udpa_http_archive(
        "bazel_skylib",
        locations = REPOSITORY_LOCATIONS,
    )
    udpa_http_archive(
        "com_envoyproxy_protoc_gen_validate",
        locations = REPOSITORY_LOCATIONS,
    )
    udpa_http_archive(
        name = "com_github_grpc_grpc",
        locations = REPOSITORY_LOCATIONS,
    )
    udpa_http_archive(
        name = "com_google_googleapis",
        locations = REPOSITORY_LOCATIONS,
    )
    udpa_http_archive(
        "com_google_protobuf",
        locations = REPOSITORY_LOCATIONS,
        # The patch includes
        # https://github.com/protocolbuffers/protobuf/pull/6333 and also uses
        # foreign_cc build for zlib as its dependency.
        # TODO(asraa): remove this when > protobuf 3.8.0 is released.
        patch_args = ["-p1"],
        patches = ["//bazel:protobuf.patch"],
    )
    udpa_http_archive(
        "io_bazel_rules_go",
        locations = REPOSITORY_LOCATIONS,
    )
    udpa_http_archive(
        name = "rules_foreign_cc",
        locations = REPOSITORY_LOCATIONS,
    )
    udpa_http_archive(
        name = "rules_proto",
        locations = REPOSITORY_LOCATIONS,
    )
    udpa_http_archive(
        name = "net_zlib",
        build_file_content = BUILD_ALL_CONTENT,
        locations = REPOSITORY_LOCATIONS,
    )
    udpa_http_archive(
        name = "six_archive",
        build_file = "@com_google_protobuf//:six.BUILD",
        locations = REPOSITORY_LOCATIONS,
    )

    # Misc. rebinds
    native.bind(
        name = "six",
        actual = "@six_archive//:six",
    )
    native.bind(
        name = "zlib",
        actual = "//bazel/foreign_cc:zlib",
    )
