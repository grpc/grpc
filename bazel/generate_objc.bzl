load(
    "//bazel:protobuf.bzl",
    "get_include_protoc_args",
    "get_plugin_args",
    "get_proto_root",
    "proto_path_to_generated_filename",
)
load(":grpc_util.bzl", "to_upper_camel_with_extension")

_GRPC_PROTO_HEADER_FMT = "{}.pbrpc.h"
_GRPC_PROTO_SRC_FMT = "{}.pbrpc.m"
_PROTO_HEADER_FMT = "{}.pbobjc.h"
_PROTO_SRC_FMT = "{}.pbobjc.m"

def generate_objc_impl(ctx):

    """Implementation of the generate_cc rule."""
    protos = [f for src in ctx.attr.srcs for f in src[ProtoInfo].check_deps_sources.to_list()]
    includes = [
        f
        for src in ctx.attr.srcs
        for f in src[ProtoInfo].transitive_imports.to_list()
    ]
    outs = []
    proto_root = get_proto_root(
        ctx.label.workspace_root,
    )

    label_package = _join_directories([ctx.label.workspace_root, ctx.label.package])
    if ctx.attr.proto_only:
        outs += [
            proto_path_to_generated_filename(
                _strip_package_from_path(label_package, proto),
                _PROTO_HEADER_FMT,
            )
            for proto in protos
        ]
        outs += [
            proto_path_to_generated_filename(
                _strip_package_from_path(label_package, proto),
                _PROTO_SRC_FMT,
            )
            for proto in protos
        ]
    else:
        outs += [
            proto_path_to_generated_filename(
                _strip_package_from_path(label_package, proto),
                _GRPC_PROTO_HEADER_FMT,
            )
            for proto in protos
        ]
        outs += [
            proto_path_to_generated_filename(
                _strip_package_from_path(label_package, proto),
                _GRPC_PROTO_SRC_FMT,
            )
            for proto in protos
        ]
    out_files = [ctx.actions.declare_file(out) for out in outs]
    dir_out = str(ctx.genfiles_dir.path + proto_root)

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
    arguments += get_include_protoc_args(includes)

    # Include the output directory so that protoc puts the generated code in the
    # right directory.
    arguments += ["--proto_path={0}{1}".format(dir_out, proto_root)]
    arguments += [_get_srcs_file_path(proto) for proto in protos]
    arguments += ["--proto_path={}".format(_get_directory_from_proto(proto)) for proto in includes]

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
        inputs = protos + includes + well_known_proto_files,
        tools = tools,
        outputs = out_files,
        executable = ctx.executable._protoc,
        arguments = arguments,
    )

    return struct(files = depset(out_files))

def _get_directory_from_proto(proto):
    return proto.path.rpartition("/")[0]

def _strip_package_from_path(label_package, file):
    prefix_len = 0
    if not file.is_source and file.path.startswith(file.root.path):
        prefix_len = len(file.root.path) + 1

    path = file.path
    file_name = ""
    if len(label_package) == 0:
        file_name = path
    if not path.startswith(label_package + "/", prefix_len) and len(label_package) > 0:
        fail("'{}' does not lie within '{}'.".format(path, label_package))
    if len(label_package) > 0:
        file_name = path[prefix_len + len(label_package + "/"):]
    
    return to_upper_camel_with_extension(file_name, "proto")

def _get_srcs_file_path(file):
    if not file.is_source and file.path.startswith(file.root.path):
        return file.path[len(file.root.path) + 1:]
    return file.path

def _join_directories(directories):
    massaged_directories = [directory for directory in directories if len(directory) != 0]
    return "/".join(massaged_directories)


generate_objc = rule(
    attrs = {
        "srcs": attr.label_list(
            mandatory = True,
            allow_empty = False,
            providers = [ProtoInfo],
        ),
        "plugin": attr.label(
            default = "@com_github_grpc_grpc//:grpc_objective_c_plugin",
            executable = True,
            providers = ["files_to_run"],
            cfg = "host",
        ),
        "proto_only": attr.bool(
            mandatory = True
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
    implementation = generate_objc_impl
)

def group_objc_files_impl(ctx):
    ext = ""
    if ctx.attr.gen_hdrs:
        ext = "h"
    else:
        ext = "m"
    out_files = [
        file 
        for file in ctx.attr.src.files.to_list() 
        if file.basename.split(".")[-1] == ext
    ]
    return struct(files = depset(out_files))

generate_objc_hdrs = rule(
    attrs = {
        "src": attr.label(
            mandatory = True,
        ),
        "gen_hdrs": attr.bool(
            default = True,
        )
    },
    implementation = group_objc_files_impl
)

generate_objc_srcs = rule(
    attrs = {
        "src": attr.label(
            mandatory = True,
        ),
        "gen_hdrs": attr.bool(
            default = False,
        )
    },
    implementation = group_objc_files_impl
)