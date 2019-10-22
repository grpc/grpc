workspace(name = "com_github_grpc_grpc")

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")
load("//bazel:grpc_deps.bzl", "grpc_deps", "grpc_test_only_deps")
load("@bazel_tools//tools/build_defs/repo:git.bzl", "git_repository")

grpc_deps()

grpc_test_only_deps()

register_execution_platforms(
    "//third_party/toolchains:rbe_windows",
)

register_toolchains(
    "//third_party/toolchains/bazel_0.26.0_rbe_windows:cc-toolchain-x64_windows",
)

load("@bazel_toolchains//rules/exec_properties:exec_properties.bzl", "create_exec_properties_dict", "custom_exec_properties", "rbe_exec_properties")

# A standard RBE execution property set repo rule.
rbe_exec_properties(
    name = "exec_properties",
)

custom_exec_properties(
    name = "grpc_custom_exec_properties",
    constants = {
        "LARGE_MACHINE": create_exec_properties_dict(gce_machine_type = "n1-standard-8"),
    },
)

load("@bazel_toolchains//rules:rbe_repo.bzl", "rbe_autoconfig")

# Create toolchain configuration for remote execution.
rbe_autoconfig(
    name = "rbe_default",
    # use exec_properties instead of deprecated remote_execution_properties
    use_legacy_platform_definition = False,
    exec_properties = create_exec_properties_dict(
        # n1-highmem-2 is the default (small machine) machine type. Targets
        # that want to use other machines (such as LARGE_MACHINE) will override
        # this value.
        gce_machine_type = "n1-highmem-2",
        docker_add_capabilities = "SYS_PTRACE",
        docker_privileged = True,
    ),
)

load("@bazel_toolchains//rules:environments.bzl", "clang_env")
load("@bazel_skylib//lib:dicts.bzl", "dicts")

# Create msan toolchain configuration for remote execution.
rbe_autoconfig(
    name = "rbe_msan",
    env = dicts.add(
        clang_env(),
        {
            "BAZEL_LINKOPTS": "-lc++:-lc++abi:-lm",
        },
    ),
)

load("@io_bazel_rules_python//python:pip.bzl", "pip_repositories", "pip_import")

pip_import(
    name = "grpc_python_dependencies",
    requirements = "@com_github_grpc_grpc//:requirements.bazel.txt",
)

load("@io_bazel_rules_python//python:pip.bzl", "pip_repositories")
load("@grpc_python_dependencies//:requirements.bzl", "pip_install")
pip_repositories()
pip_install()

load("@com_google_protobuf//:protobuf_deps.bzl", "protobuf_deps")
protobuf_deps()

load("@upb//bazel:workspace_deps.bzl", "upb_deps")
upb_deps()

load("@envoy_api//bazel:repositories.bzl", "api_dependencies")
api_dependencies()

load("@io_bazel_rules_go//go:deps.bzl", "go_rules_dependencies", "go_register_toolchains")
go_rules_dependencies()
go_register_toolchains()


load("@build_bazel_rules_apple//apple:repositories.bzl", "apple_rules_dependencies")
apple_rules_dependencies()

load("@build_bazel_apple_support//lib:repositories.bzl", "apple_support_dependencies")
apple_support_dependencies()
