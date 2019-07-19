load(
    "//bazel:protobuf.bzl",
    "get_include_protoc_args",
    "get_plugin_args",
    "get_proto_root",
    "proto_path_to_generated_filename",
)
load(
    ":grpc_util.bzl", 
    "to_upper_camel_with_extension",
    "label_to_file",
)

_GRPC_PROTO_HEADER_FMT = "{}.pbrpc.h"
_GRPC_PROTO_SRC_FMT = "{}.pbrpc.m"
_PROTO_HEADER_FMT = "{}.pbobjc.h"
_PROTO_SRC_FMT = "{}.pbobjc.m"
_GENERATED_PROTOS_DIR = "_generated_protos"

def _generate_objc_impl(ctx):

    """Implementation of the generate_cc rule."""
    protos = [
        f
        for src in ctx.attr.deps
        for f in src[ProtoInfo].transitive_imports.to_list()
    ]
    outs = []
    proto_root = get_proto_root(
        ctx.label.workspace_root,
    )

    label_package = _join_directories([ctx.label.workspace_root, ctx.label.package])

    files_with_rpc = [label_to_file(f) for f in ctx.attr.srcs]
    for proto in protos:
        outs += [_get_file_out_from_proto(proto, _PROTO_HEADER_FMT)]
        outs += [_get_file_out_from_proto(proto, _PROTO_SRC_FMT)]

        file_path = _strip_package_from_path(label_package, proto)
        if not file_path.startswith("//"):
            pass # TODO: check package boundary
        if file_path in files_with_rpc or proto.path in files_with_rpc:
            outs += [_get_file_out_from_proto(proto, _GRPC_PROTO_HEADER_FMT)]
            outs += [_get_file_out_from_proto(proto, _GRPC_PROTO_SRC_FMT)]
    
    out_files = [ctx.actions.declare_file(out) for out in outs]
    dir_out = _join_directories([
        str(ctx.genfiles_dir.path + proto_root), label_package, _GENERATED_PROTOS_DIR
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
    arguments += get_include_protoc_args(protos)

    # Include the output directory so that protoc puts the generated code in the
    # right directory.
    arguments += ["--proto_path={0}{1}".format(dir_out, proto_root)]
    arguments += ["--proto_path={}".format(_get_directory_from_proto(proto)) for proto in protos]
    arguments += [_get_srcs_file_path(proto) for proto in protos]

    # create a list of well known proto files if the argument is non-None
    well_known_proto_files = []
    if ctx.attr.use_well_known_protos:
        f = ctx.attr.well_known_protos.files.to_list()[0].dirname
        arguments += ["-I{0}".format(f + "/../..")]
        well_known_proto_files = [
            f
            for f in ctx.attr.well_known_protos.files.to_list()
        ]
    ctx.actions.run(
        inputs = protos + well_known_proto_files,
        tools = tools,
        outputs = out_files,
        executable = ctx.executable._protoc,
        arguments = arguments,
    )

    return struct(files = depset(out_files))

def _get_file_out_from_proto(proto, fmt):
    return proto_path_to_generated_filename(
        _GENERATED_PROTOS_DIR + "/" +
        _get_directory_from_proto(proto) + _get_slash_from_proto(proto) +
        to_upper_camel_with_extension(_get_file_name_from_proto(proto), "proto"),
        fmt,
    )

def _get_file_name_from_proto(proto):
    return proto.path.rpartition("/")[2]

def _get_slash_from_proto(proto):
    return proto.path.rpartition("/")[1]

def _get_directory_from_proto(proto):
    return proto.path.rpartition("/")[0]

def _strip_package_and_to_camel_case(label_package, file):
    return to_upper_camel_with_extension(
        _strip_package_from_path(label_package, file),
        "proto"
    )

def _strip_package_from_path(label_package, file):
    prefix_len = 0
    if not file.is_source and file.path.startswith(file.root.path):
        prefix_len = len(file.root.path) + 1

    path = file.path
    file_name = ""
    if len(label_package) == 0:
        file_name = path
    if not path.startswith(label_package + "/", prefix_len) and len(label_package) > 0:
        return "//" + file.path
    if len(label_package) > 0:
        file_name = path[prefix_len + len(label_package + "/"):]
    
    return file_name

def _get_srcs_file_path(file):
    if not file.is_source and file.path.startswith(file.root.path):
        return file.path[len(file.root.path) + 1:]
    return file.path

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
            allow_empty = True
        ),
        "use_well_known_protos": attr.bool(
            mandatory = False,
            default = False
        ),
        "well_known_protos": attr.label(
            default = "@com_google_protobuf//:well_known_protos"
        ),
        "_protoc": attr.label(
            default = Label("//external:protocol_compiler"),
            executable = True,
            cfg = "host",
        ),
    },
    output_to_genfiles = True,
    implementation = _generate_objc_impl
)

def _group_objc_files_impl(ctx):
    suffix = ""
    if ctx.attr.gen_mode == 1:
        suffix = "h"
    elif ctx.attr.gen_mode == 2:
        suffix = "pbrpc.m"
    elif ctx.attr.gen_mode == 3:
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
            default = 1,
        )
    },
    implementation = _group_objc_files_impl
)

generate_objc_srcs = rule(
    attrs = {
        "src": attr.label(
            mandatory = True,
        ),
        "gen_mode": attr.int(
            default = 2,
        )
    },
    implementation = _group_objc_files_impl
)

generate_objc_non_arc_srcs = rule(
    attrs = {
        "src": attr.label(
            mandatory = True,
        ),
        "gen_mode": attr.int(
            default = 3,
        )
    },
    implementation = _group_objc_files_impl
)
