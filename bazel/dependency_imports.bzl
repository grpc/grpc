load("@bazel_gazelle//:deps.bzl", "gazelle_dependencies")
load("@com_google_protobuf//:protobuf_deps.bzl", "protobuf_deps")
load("@io_bazel_rules_go//go:deps.bzl", "go_register_toolchains", "go_rules_dependencies")
load("@io_bazel_rules_python//python:pip.bzl", "pip_import", "pip_repositories")
load("@rules_proto//proto:repositories.bzl", "rules_proto_dependencies", "rules_proto_toolchains")

# Only needed for PIP support:

def _pgv_pip_dependencies():
    pip_repositories()

    # This rule translates the specified requirements.txt into
    # @pgv_pip_deps//:requirements.bzl, which itself exposes a pip_install method.
    pip_import(
        name = "pgv_pip_deps",
        requirements = "//:requirements.txt",
    )

def _pgv_go_dependencies():
    go_rules_dependencies()
    go_register_toolchains(
        version = "1.15.6",
    )
    gazelle_dependencies()

def pgv_dependency_imports():
    # Import @com_google_protobuf's dependencies.
    protobuf_deps()

    # Import @pgv_pip_deps defined by pip's requirements.txt.
    _pgv_pip_dependencies()

    # Import rules for the Go compiler.
    _pgv_go_dependencies()

    # Setup rules_proto.
    rules_proto_dependencies()
    rules_proto_toolchains()
