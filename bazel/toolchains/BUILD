load("@bazel_skylib//rules:common_settings.bzl", "bool_flag")

package(default_visibility = ["//visibility:public"])

bool_flag(
    name = "enable_plugin_toolchain_resolution",
)

toolchain_type(name = "cpp_plugin_toolchain_type")
toolchain_type(name = "objective_c_plugin_toolchain_type")
toolchain_type(name = "python_plugin_toolchain_type")
