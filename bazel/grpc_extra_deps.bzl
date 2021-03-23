"""Loads the dependencies necessary for the external repositories defined in grpc_deps.bzl."""

load("@com_google_protobuf//:protobuf_deps.bzl", "protobuf_deps")
load("@upb//bazel:workspace_deps.bzl", "upb_deps")
load("@envoy_api//bazel:repositories.bzl", "api_dependencies")
load("@io_bazel_rules_go//go:deps.bzl", "go_register_toolchains", "go_rules_dependencies")
load("@build_bazel_rules_apple//apple:repositories.bzl", "apple_rules_dependencies")
load("@build_bazel_apple_support//lib:repositories.bzl", "apple_support_dependencies")
load("@com_google_googleapis//:repository_rules.bzl", "switched_rules_by_language")

def grpc_extra_deps(ignore_version_differences = False):
    """Loads the extra dependencies.

    These are necessary for using the external repositories defined in
    grpc_deps.bzl. Projects that depend on gRPC as an external repository need
    to call both grpc_deps and grpc_extra_deps, if they have not already loaded
    the extra dependencies. For example, they can do the following in their
    WORKSPACE
    ```
    load("@com_github_grpc_grpc//bazel:grpc_deps.bzl", "grpc_deps", "grpc_test_only_deps")
    grpc_deps()

    grpc_test_only_deps()

    load("@com_github_grpc_grpc//bazel:grpc_extra_deps.bzl", "grpc_extra_deps")

    grpc_extra_deps()
    ```
    """
    protobuf_deps()

    upb_deps()

    api_dependencies()

    go_rules_dependencies()
    go_register_toolchains()

    apple_rules_dependencies(ignore_version_differences = ignore_version_differences)

    apple_support_dependencies()

    # Initialize Google APIs with only C++ and Python targets
    switched_rules_by_language(
        name = "com_google_googleapis_imports",
        cc = True,
        grpc = True,
        python = True,
    )
