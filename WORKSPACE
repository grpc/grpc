workspace(name = "com_github_grpc_grpc")

load("//bazel:grpc_deps.bzl", "grpc_deps", "grpc_test_only_deps")

grpc_deps()

grpc_test_only_deps()

load("//bazel:grpc_extra_deps.bzl", "grpc_extra_deps")

grpc_extra_deps()

register_execution_platforms(
    "//third_party/toolchains:rbe_windows",
)

register_toolchains(
    "//third_party/toolchains/bazel_0.26.0_rbe_windows:cc-toolchain-x64_windows",
)

load("@bazel_toolchains//rules/exec_properties:exec_properties.bzl", "create_exec_properties_dict", "custom_exec_properties")

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
    exec_properties = create_exec_properties_dict(
        docker_add_capabilities = "SYS_PTRACE",
        docker_privileged = True,
        # n1-highmem-2 is the default (small machine) machine type. Targets
        # that want to use other machines (such as LARGE_MACHINE) will override
        # this value.
        gce_machine_type = "n1-highmem-2",
    ),
    # use exec_properties instead of deprecated remote_execution_properties
    use_legacy_platform_definition = False,
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

load("@io_bazel_rules_python//python:pip.bzl", "pip_import", "pip_repositories")

pip_import(
    name = "grpc_python_dependencies",
    requirements = "@com_github_grpc_grpc//:requirements.bazel.txt",
)

load("@grpc_python_dependencies//:requirements.bzl", "pip_install")

pip_repositories()

pip_install()
