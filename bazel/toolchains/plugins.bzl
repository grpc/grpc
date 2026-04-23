"""Rules for defining Bazel toolchains for Protobuf plugins for gRPC."""

GrpcPluginInfo = provider(
    doc = "Information about how to invoke a Protobuf compiler plugin for gRPC.",
    fields = [
        # File object of a platform-specific plugin executable.
        "bin",
        # List of string-valued command-line options to pass to the plugin.
        "options",
    ],
)

def _plugin_toolchain_impl(ctx):
    toolchain_info = platform_common.ToolchainInfo(
        plugin = GrpcPluginInfo(
            bin = ctx.executable.bin,
            options = ctx.attr.options,
        ),
    )
    return [toolchain_info]

plugin_toolchain = rule(
    implementation = _plugin_toolchain_impl,
    attrs = {
        "bin": attr.label(
            doc = "Plugin executable.",
            mandatory = True,
            executable = True,
            cfg = "exec",
            allow_single_file = True,
        ),
        "options": attr.string_list(
            doc = "Options to pass to the plugin.",
        ),
    },
)
