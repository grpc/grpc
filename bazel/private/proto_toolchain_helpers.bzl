# Adapted from https://github.com/protocolbuffers/protobuf/blob/v33.2/bazel/private/toolchain_helpers.bzl
"""
Toolchain helpers.

The helpers here should be used for a migration to toolchain in proto rules.

Anybody that needs them in another repository should copy them, because after
the migration is finished, the helpers can be removed.
"""

load("@com_google_protobuf//bazel/common:proto_lang_toolchain_info.bzl", "ProtoLangToolchainInfo")
load("@com_google_protobuf//bazel/private:native.bzl", "native_proto_common")

_incompatible_toolchain_resolution = getattr(native_proto_common, "INCOMPATIBLE_ENABLE_PROTO_TOOLCHAIN_RESOLUTION", False)
_proto_toolchain = Label("@com_google_protobuf//bazel/private:proto_toolchain_type")

def _find_protoc(ctx, legacy_attr):
    if _incompatible_toolchain_resolution:
        toolchain = ctx.toolchains[_proto_toolchain]
        if not toolchain:
            fail("Protocol compiler toolchain could not be resolved.")
        return toolchain.proto.proto_compiler
    else:
        return getattr(ctx.executable, legacy_attr)

def _find_toolchain(ctx, legacy_attr, toolchain_type):
    if _incompatible_toolchain_resolution:
        toolchain = ctx.toolchains[toolchain_type]
        if not toolchain:
            fail("No toolchains registered for '%s'." % toolchain_type)
        return toolchain.proto
    else:
        return getattr(ctx.attr, legacy_attr)[ProtoLangToolchainInfo]

def _use_toolchain(toolchain_type):
    if _incompatible_toolchain_resolution:
        return [config_common.toolchain_type(toolchain_type, mandatory = False)]
    else:
        return []

def _if_legacy_toolchain(legacy_attr_dict):
    if _incompatible_toolchain_resolution:
        return {}
    else:
        return legacy_attr_dict

toolchains = struct(
    use_toolchain = _use_toolchain,
    find_protoc = _find_protoc,
    find_toolchain = _find_toolchain,
    if_legacy_toolchain = _if_legacy_toolchain,
    INCOMPATIBLE_ENABLE_PROTO_TOOLCHAIN_RESOLUTION = _incompatible_toolchain_resolution,
    PROTO_TOOLCHAIN = _proto_toolchain,
)
