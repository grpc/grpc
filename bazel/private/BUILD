load("@bazel_skylib//rules:write_file.bzl", "write_file")

config_setting(
    name = "plugin_toolchain_resolution_enabled",
    flag_values = {
        "//bazel/toolchains:enable_plugin_toolchain_resolution": "True",
    },
)

# Whereas the Protobuf toolchain migration made use of an internal Bazel setting
# (`--incompatible_enable_proto_toolchain_resolution`) that can be read during the loading phase,
# the gRPC toolchain migration only makes use of a standard Bazel config setting
# (`//bazel/toolchains:enable_plugin_toolchain_resolution`).
# This requires using `select` to make the fallback plugins (built from source) optional.
# When resolution is enabled, swap the real fallback plugins out for this empty "dummy executable".
# In that case, the dummy fallback plugin, which is not a valid executable,
# must be ignored during the analysis phase.
write_file(
    name = "dummy_executable",
    out = "dummy_executable",
    is_executable = True,
)

alias(
    name = "maybe_grpc_cpp_plugin",
    actual = select({
        ":plugin_toolchain_resolution_enabled": ":dummy_executable",
        "//conditions:default": "//src/compiler:grpc_cpp_plugin",
    }),
)

alias(
    name = "maybe_grpc_objective_c_plugin",
    actual = select({
        ":plugin_toolchain_resolution_enabled": ":dummy_executable",
        "//conditions:default": "//src/compiler:grpc_objective_c_plugin",
    }),
)

alias(
    name = "maybe_grpc_python_plugin",
    actual = select({
        ":plugin_toolchain_resolution_enabled": ":dummy_executable",
        "//conditions:default": "//src/compiler:grpc_python_plugin",
    }),
)
