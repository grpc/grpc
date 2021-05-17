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

load("@bazel_toolchains//rules/exec_properties:exec_properties.bzl", "create_exec_properties_dict", "custom_exec_properties", "merge_dicts")

custom_exec_properties(
    name = "grpc_custom_exec_properties",
    constants = {
        "LARGE_MACHINE": merge_dicts(
            create_exec_properties_dict(),
            # TODO(jtattermusch): specifying 'labels = {"abc": "xyz"}' in create_exec_properties_dict
            # is not possible without https://github.com/bazelbuild/bazel-toolchains/pull/748
            # and currently the toolchain we're using is too old for that. To be able to select worker
            # pools through labels, we use a workaround and populate the corresponding label values
            # manually (see create_exec_properties_dict logic for how labels get transformed)
            # Remove this workaround once we transition to a new-enough bazel toolchain.
            # The next line corresponds to 'labels = {"os": "ubuntu", "machine_size": "large"}'
            {
                "label:os": "ubuntu",
                "label:machine_size": "large",
            },
        ),
    },
)

load("@bazel_toolchains//rules:rbe_repo.bzl", "rbe_autoconfig")

# Create toolchain configuration for remote execution.
rbe_autoconfig(
    name = "rbe_default",
    exec_properties = merge_dicts(
        create_exec_properties_dict(
            docker_add_capabilities = "SYS_PTRACE",
            docker_privileged = True,
            os_family = "Linux",
        ),
        # TODO(jtattermusch): specifying 'labels = {"abc": "xyz"}' in create_exec_properties_dict
        # is not possible without https://github.com/bazelbuild/bazel-toolchains/pull/748
        # and currently the toolchain we're using is too old for that. To be able to select worker
        # pools through labels, we use a workaround and populate the corresponding label values
        # manually (see create_exec_properties_dict logic for how labels get transformed)
        # Remove this workaround once we transition to a new-enough bazel toolchain.
        # The next line corresponds to 'labels = {"os": "ubuntu", "machine_size": "small"}'
        {
            "label:os": "ubuntu",
            "label:machine_size": "small",
        },
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
