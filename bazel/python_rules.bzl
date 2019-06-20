"""Generates and compiles Python gRPC stubs from proto_library rules."""

load("@grpc_python_dependencies//:requirements.bzl", "requirement")
load(
    "//bazel:protobuf.bzl",
    "get_include_protoc_args",
    "get_plugin_args",
    "get_proto_root",
    "proto_path_to_generated_filename",
)

_GENERATED_PROTO_FORMAT = "{}_pb2.py"
_GENERATED_GRPC_PROTO_FORMAT = "{}_pb2_grpc.py"

def _get_staged_proto_file(context, source_file):
    if source_file.dirname == context.label.package:
        return source_file
    else:
        copied_proto = context.actions.declare_file(source_file.basename)
        context.actions.run_shell(
            inputs = [source_file],
            outputs = [copied_proto],
            command = "cp {} {}".format(source_file.path, copied_proto.path),
            mnemonic = "CopySourceProto",
        )
        return copied_proto

def _generate_py_impl(context):
    protos = []
    for src in context.attr.deps:
        for file in src[ProtoInfo].direct_sources:
            protos.append(_get_staged_proto_file(context, file))
    includes = [
        file
        for src in context.attr.deps
        for file in src[ProtoInfo].transitive_imports.to_list()
    ]
    proto_root = get_proto_root(context.label.workspace_root)
    format_str = (_GENERATED_GRPC_PROTO_FORMAT if context.executable.plugin else _GENERATED_PROTO_FORMAT)
    out_files = [
        context.actions.declare_file(
            proto_path_to_generated_filename(
                proto.basename,
                format_str,
            ),
        )
        for proto in protos
    ]

    arguments = []
    tools = [context.executable._protoc]
    if context.executable.plugin:
        arguments += get_plugin_args(
            context.executable.plugin,
            context.attr.flags,
            context.genfiles_dir.path,
            False,
        )
        tools += [context.executable.plugin]
    else:
        arguments += [
            "--python_out={}:{}".format(
                ",".join(context.attr.flags),
                context.genfiles_dir.path,
            ),
        ]

    arguments += get_include_protoc_args(includes)
    arguments += [
        "--proto_path={}".format(context.genfiles_dir.path)
        for proto in protos
    ]
    for proto in protos:
        massaged_path = proto.path
        if massaged_path.startswith(context.genfiles_dir.path):
            massaged_path = proto.path[len(context.genfiles_dir.path) + 1:]
        arguments.append(massaged_path)

    well_known_proto_files = []
    if context.attr.well_known_protos:
        well_known_proto_directory = context.attr.well_known_protos.files.to_list(
        )[0].dirname

        arguments += ["-I{}".format(well_known_proto_directory + "/../..")]
        well_known_proto_files = context.attr.well_known_protos.files.to_list()

    context.actions.run(
        inputs = protos + includes + well_known_proto_files,
        tools = tools,
        outputs = out_files,
        executable = context.executable._protoc,
        arguments = arguments,
        mnemonic = "ProtocInvocation",
    )
    return struct(files = depset(out_files))

__generate_py = rule(
    attrs = {
        "deps": attr.label_list(
            mandatory = True,
            allow_empty = False,
            providers = [ProtoInfo],
        ),
        "plugin": attr.label(
            executable = True,
            providers = ["files_to_run"],
            cfg = "host",
        ),
        "flags": attr.string_list(
            mandatory = False,
            allow_empty = True,
        ),
        "well_known_protos": attr.label(mandatory = False),
        "_protoc": attr.label(
            default = Label("//external:protocol_compiler"),
            executable = True,
            cfg = "host",
        ),
    },
    output_to_genfiles = True,
    implementation = _generate_py_impl,
)

def _generate_py(well_known_protos, **kwargs):
    if well_known_protos:
        __generate_py(
            well_known_protos = "@com_google_protobuf//:well_known_protos",
            **kwargs
        )
    else:
        __generate_py(**kwargs)

def py_proto_library(
        name,
        deps,
        well_known_protos = True,
        proto_only = False,
        **kwargs):
    """Generate python code for a protobuf.

    Args:
      name: The name of the target.
      deps: A list of dependencies. Must contain a single element.
      well_known_protos: A bool indicating whether or not to include well-known
        protos.
      proto_only: A bool indicating whether to generate vanilla protobuf code
        or to also generate gRPC code.
    """
    if len(deps) > 1:
        fail("The supported length of 'deps' is 1.")

    codegen_target = "_{}_codegen".format(name)
    codegen_grpc_target = "_{}_grpc_codegen".format(name)

    _generate_py(
        name = codegen_target,
        deps = deps,
        well_known_protos = well_known_protos,
        **kwargs
    )

    if not proto_only:
        _generate_py(
            name = codegen_grpc_target,
            deps = deps,
            plugin = "//:grpc_python_plugin",
            well_known_protos = well_known_protos,
            **kwargs
        )

        native.py_library(
            name = name,
            srcs = [
                ":{}".format(codegen_grpc_target),
                ":{}".format(codegen_target),
            ],
            deps = [requirement("protobuf")],
            **kwargs
        )
    else:
        native.py_library(
            name = name,
            srcs = [":{}".format(codegen_target), ":{}".format(codegen_target)],
            deps = [requirement("protobuf")],
            **kwargs
        )
