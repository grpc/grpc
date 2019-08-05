"""Generates and compiles Python gRPC stubs from proto_library rules."""

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
    out_files = [
        context.actions.declare_file(
            proto_path_to_generated_filename(
                proto.basename,
                _GENERATED_PROTO_FORMAT,
            ),
        )
        for proto in protos
    ]

    tools = [context.executable._protoc]
    arguments = ([
        "--python_out={}".format(
            context.genfiles_dir.path,
        ),
    ] + get_include_protoc_args(includes) + [
        "--proto_path={}".format(context.genfiles_dir.path)
        for proto in protos
    ])
    for proto in protos:
        massaged_path = proto.path
        if massaged_path.startswith(context.genfiles_dir.path):
            massaged_path = proto.path[len(context.genfiles_dir.path) + 1:]
        arguments.append(massaged_path)

    context.actions.run(
        inputs = protos + includes,
        tools = tools,
        outputs = out_files,
        executable = context.executable._protoc,
        arguments = arguments,
        mnemonic = "ProtocInvocation",
    )
    return struct(files = depset(out_files))

_generate_pb2_src = rule(
    attrs = {
        "deps": attr.label_list(
            mandatory = True,
            allow_empty = False,
            providers = [ProtoInfo],
        ),
        "_protoc": attr.label(
            default = Label("//external:protocol_compiler"),
            providers = ["files_to_run"],
            executable = True,
            cfg = "host",
        ),
    },
    implementation = _generate_py_impl,
)

def py_proto_library(
        name,
        srcs,
        **kwargs):
    """Generate python code for a protobuf.

    Args:
      name: The name of the target.
      srcs: A list of proto_library dependencies. Must contain a single element.
    """
    codegen_target = "_{}_codegen".format(name)
    if len(srcs) > 1:
        fail("Can only compile a single proto at a time.")


    _generate_pb2_src(
        name = codegen_target,
        deps = srcs,
        **kwargs
    )

    native.py_library(
        name = name,
        srcs = [":{}".format(codegen_target)],
        deps = ["@com_google_protobuf//:protobuf_python"],
        **kwargs
    )

def _generate_pb2_grpc_src_impl(context):
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
    out_files = [
        context.actions.declare_file(
            proto_path_to_generated_filename(
                proto.basename,
                _GENERATED_GRPC_PROTO_FORMAT,
            ),
        )
        for proto in protos
    ]

    arguments = []
    tools = [context.executable._protoc, context.executable._plugin]
    arguments += get_plugin_args(
        context.executable._plugin,
        [],
        context.genfiles_dir.path,
        False,
    )

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

    context.actions.run(
        inputs = protos + includes,
        tools = tools,
        outputs = out_files,
        executable = context.executable._protoc,
        arguments = arguments,
        mnemonic = "ProtocInvocation",
    )
    return struct(files = depset(out_files))


_generate_pb2_grpc_src = rule(
    attrs = {
        "deps": attr.label_list(
            mandatory = True,
            allow_empty = False,
            providers = [ProtoInfo],
        ),
        "_plugin": attr.label(
            executable = True,
            providers = ["files_to_run"],
            cfg = "host",
            default = Label("//src/compiler:grpc_python_plugin"),
        ),
        "_protoc": attr.label(
            executable = True,
            providers = ["files_to_run"],
            cfg = "host",
            default = Label("//external:protocol_compiler"),
        ),
    },
    implementation = _generate_pb2_grpc_src_impl,
)

def py_grpc_library(
    name,
    srcs,
    deps,
    **kwargs):
    """Generate python code for gRPC services defined in a protobuf.

    Args:
      name: The name of the target.
      srcs: (List of `labels`) a single proto_library target containing the
        schema of the service.
      deps: (List of `labels`) a single py_proto_library target for the
        proto_library in `srcs`.
    """
    codegen_grpc_target = "_{}_grpc_codegen".format(name)
    if len(srcs) > 1:
        fail("Can only compile a single proto at a time.")

    if len(deps) > 1:
        fail("Deps must have length 1.")

    _generate_pb2_grpc_src(
        name = codegen_grpc_target,
        deps = srcs,
        **kwargs
    )

    native.py_library(
        name = name,
        srcs = [
            ":{}".format(codegen_grpc_target),
        ],
        deps = [Label("//src/python/grpcio/grpc:grpcio")] + deps,
        **kwargs
    )
