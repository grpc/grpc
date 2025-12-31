"""
Protobuf compiler plugin helpers.

Similar to `proto_toolchain_helpers`,
this library contains helper methods to be used for a graceful migration to toolchains
for language-specific gRPC plugins.

The `find_toolchain` and `options` methods must be called from rules
that have a label attribute named `_enable_plugin_toolchain_resolution`
which references the boolean flag `//bazel/toolchains:enable_plugin_toolchain_resolution`.
"""

load("@bazel_skylib//rules:common_settings.bzl", "BuildSettingInfo")

_toolchains_enabled_setting = Label("//bazel/toolchains:enable_plugin_toolchain_resolution")

cpp_plugin_toolchain = Label("//bazel/toolchains:cpp_plugin_toolchain_type")
objective_c_plugin_toolchain = Label("//bazel/toolchains:objective_c_plugin_toolchain_type")
python_plugin_toolchain = Label("//bazel/toolchains:python_plugin_toolchain_type")

def _find_toolchain(ctx, legacy_attr, toolchain_type):
    if ctx.attr._enable_plugin_toolchain_resolution[BuildSettingInfo].value:
        toolchain = ctx.toolchains[toolchain_type]
        if not toolchain:
            fail("No toolchains registered for '%s'." % toolchain_type)
        return toolchain.plugin.bin
    else:
        return getattr(ctx.executable, legacy_attr)

def _options(ctx, default_options, toolchain_type):
    if ctx.attr._enable_plugin_toolchain_resolution[BuildSettingInfo].value:
        toolchain = ctx.toolchains[toolchain_type]
        if not toolchain:
            fail("No toolchains registered for '%s'." % toolchain_type)
        return toolchain.plugin.options
    else:
        return default_options

def _use_toolchain(toolchain_type):
    return [config_common.toolchain_type(toolchain_type, mandatory = False)]

toolchains = struct(
    find_toolchain = _find_toolchain,
    options = _options,
    use_toolchain = _use_toolchain,
)
