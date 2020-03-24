load("@rules_proto//proto:defs.bzl", "ProtoInfo")
load(
    "//bazel:protobuf.bzl",
    "get_include_directory",
    "get_plugin_args",
    "proto_path_to_generated_filename",
)
load(":grpc_util.bzl", "to_upper_camel_with_extension")

_GRPC_PROTO_HEADER_FMT = "{}.pbrpc.h"
_GRPC_PROTO_SRC_FMT = "{}.pbrpc.m"
_PROTO_HEADER_FMT = "{}.pbobjc.h"
_PROTO_SRC_FMT = "{}.pbobjc.m"
_GENERATED_PROTOS_DIR = "_generated_protos"

_GENERATE_HDRS = 1
_GENERATE_SRCS = 2
_GENERATE_NON_ARC_SRCS = 3

def _generate_objc_impl(ctx):
    """Implementation of the generate_objc rule."""
    protos = [
        f
        for src in ctx.attr.deps
        for f in src[ProtoInfo].transitive_imports.to_list()
    ]

    target_package = _join_directories([ctx.label.workspace_root, ctx.label.package])

    files_with_rpc = [_label_to_full_file_path(f, target_package) for f in ctx.attr.srcs]

    outs = []
    for proto in protos:
        outs += [_get_output_file_name_from_proto(proto, _PROTO_HEADER_FMT)]
        outs += [_get_output_file_name_from_proto(proto, _PROTO_SRC_FMT)]

        file_path = _get_full_path_from_file(proto)
        if file_path in files_with_rpc:
            outs += [_get_output_file_name_from_proto(proto, _GRPC_PROTO_HEADER_FMT)]
            outs += [_get_output_file_name_from_proto(proto, _GRPC_PROTO_SRC_FMT)]

    out_files = [ctx.actions.declare_file(out) for out in outs]
    dir_out = _join_directories([
        str(ctx.genfiles_dir.path),
        target_package,
        _GENERATED_PROTOS_DIR,
    ])

    arguments = []
    if ctx.executable.plugin:
        arguments += get_plugin_args(
            ctx.executable.plugin,
            [],
            dir_out,
            False,
        )
        tools = [ctx.executable.plugin]
    arguments += ["--objc_out=" + dir_out]

    arguments += ["--proto_path=."]
    arguments += [
        "--proto_path={}".format(get_include_directory(i))
        for i in protos
    ]

    # Include the output directory so that protoc puts the generated code in the
    # right directory.
    arguments += ["--proto_path={}".format(dir_out)]
    arguments += ["--proto_path={}".format(_get_directory_from_proto(proto)) for proto in protos]
    arguments += [_get_full_path_from_file(proto) for proto in protos]

    # create a list of well known proto files if the argument is non-None
    well_known_proto_files = []
    if ctx.attr.use_well_known_protos:
        f = ctx.attr.well_known_protos.files.to_list()[0].dirname

        # go two levels up so that #import "google/protobuf/..." is correct
        arguments += ["-I{0}".format(f + "/../..")]
        well_known_proto_files = ctx.attr.well_known_protos.files.to_list()
    ctx.actions.run(
        inputs = protos + well_known_proto_files,
        tools = tools,
        outputs = out_files,
        executable = ctx.executable._protoc,
        arguments = arguments,
    )

    return struct(files = depset(out_files))

def _label_to_full_file_path(src, package):
    if not src.startswith("//"):
        # Relative from current package
        if not src.startswith(":"):
            # "a.proto" -> ":a.proto"
            src = ":" + src
        src = "//" + package + src

    # Converts //path/to/package:File.ext to path/to/package/File.ext.
    src = src.replace("//", "")
    src = src.replace(":", "/")
    if src.startswith("/"):
        # "//:a.proto" -> "/a.proto" so remove the initial slash
        return src[1:]
    else:
        return src

def _get_output_file_name_from_proto(proto, fmt):
    return proto_path_to_generated_filename(
        _GENERATED_PROTOS_DIR + "/" +
        _get_directory_from_proto(proto) + _get_slash_or_null_from_proto(proto) +
        to_upper_camel_with_extension(_get_file_name_from_proto(proto), "proto"),
        fmt,
    )

def _get_file_name_from_proto(proto):
    return proto.path.rpartition("/")[2]

def _get_slash_or_null_from_proto(proto):
    """Potentially returns empty (if the file is in the root directory)"""
    return proto.path.rpartition("/")[1]

def _get_directory_from_proto(proto):
    return proto.path.rpartition("/")[0]

def _get_full_path_from_file(file):
    gen_dir_length = 0

    # if file is generated, then prepare to remote its root
    # (including CPU architecture...)
    if not file.is_source:
        gen_dir_length = len(file.root.path) + 1

    return file.path[gen_dir_length:]

def _join_directories(directories):
    massaged_directories = [directory for directory in directories if len(directory) != 0]
    return "/".join(massaged_directories)

generate_objc = rule(
    attrs = {
        "deps": attr.label_list(
            mandatory = True,
            allow_empty = False,
            providers = [ProtoInfo],
        ),
        "plugin": attr.label(
            default = "@com_github_grpc_grpc//src/compiler:grpc_objective_c_plugin",
            executable = True,
            providers = ["files_to_run"],
            cfg = "host",
        ),
        "srcs": attr.string_list(
            mandatory = False,
            allow_empty = True,
        ),
        "use_well_known_protos": attr.bool(
            mandatory = False,
            default = False,
        ),
        "well_known_protos": attr.label(
            default = "@com_google_protobuf//:well_known_protos",
        ),
        "_protoc": attr.label(
            default = Label("//external:protocol_compiler"),
            executable = True,
            cfg = "host",
        ),
    },
    output_to_genfiles = True,
    implementation = _generate_objc_impl,
)

def _group_objc_files_impl(ctx):
    suffix = ""
    if ctx.attr.gen_mode == _GENERATE_HDRS:
        suffix = "h"
    elif ctx.attr.gen_mode == _GENERATE_SRCS:
        suffix = "pbrpc.m"
    elif ctx.attr.gen_mode == _GENERATE_NON_ARC_SRCS:
        suffix = "pbobjc.m"
    else:
        fail("Undefined gen_mode")
    out_files = [
        file
        for file in ctx.attr.src.files.to_list()
        if file.basename.endswith(suffix)
    ]
    return struct(files = depset(out_files))

generate_objc_hdrs = rule(
    attrs = {
        "src": attr.label(
            mandatory = True,
        ),
        "gen_mode": attr.int(
            default = _GENERATE_HDRS,
        ),
    },
    implementation = _group_objc_files_impl,
)

generate_objc_srcs = rule(
    attrs = {
        "src": attr.label(
            mandatory = True,
        ),
        "gen_mode": attr.int(
            default = _GENERATE_SRCS,
        ),
    },
    implementation = _group_objc_files_impl,
)

generate_objc_non_arc_srcs = rule(
    attrs = {
        "src": attr.label(
            mandatory = True,
        ),
        "gen_mode": attr.int(
            default = _GENERATE_NON_ARC_SRCS,
        ),
    },
    implementation = _group_objc_files_impl,
)
