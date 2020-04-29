"""Generates and compiles Python gRPC stubs from proto_library rules."""

load("@rules_proto//proto:defs.bzl", "ProtoInfo")
load(
    "//bazel:protobuf.bzl",
    "declare_out_files",
    "get_include_directory",
    "get_out_dir",
    "get_plugin_args",
    "get_proto_arguments",
    "includes_from_deps",
    "protos_from_context",
)

_GENERATED_PROTO_FORMAT = "{}_pb2.py"
_GENERATED_GRPC_PROTO_FORMAT = "{}_pb2_grpc.py"

def _generate_py_impl(context):
    protos = protos_from_context(context)
    includes = includes_from_deps(context.attr.deps)
    out_files = declare_out_files(protos, context, _GENERATED_PROTO_FORMAT)
    tools = [context.executable._protoc]

    out_dir = get_out_dir(protos, context)
    arguments = ([
        "--python_out={}".format(out_dir.path),
    ] + [
        "--proto_path={}".format(get_include_directory(i))
        for i in includes
    ] + [
        "--proto_path={}".format(context.genfiles_dir.path),
    ])
    if context.attr.plugin:
        arguments += get_plugin_args(
            context.executable.plugin,
            [],
            out_dir.path,
            False,
            context.attr.plugin.label.name,
        )
        tools.append(context.executable.plugin)

    arguments += get_proto_arguments(protos, context.genfiles_dir.path)

    context.actions.run(
        inputs = protos + includes,
        tools = tools,
        outputs = out_files,
        executable = context.executable._protoc,
        arguments = arguments,
        mnemonic = "ProtocInvocation",
    )

    imports = []
    if out_dir.import_path:
        imports.append("%s/%s/%s" % (context.workspace_name, context.label.package, out_dir.import_path))

    return [
        DefaultInfo(files = depset(direct = out_files)),
        PyInfo(
            transitive_sources = depset(),
            imports = depset(direct = imports),
        ),
    ]

_generate_pb2_src = rule(
    attrs = {
        "deps": attr.label_list(
            mandatory = True,
            allow_empty = False,
            providers = [ProtoInfo],
        ),
        "plugin": attr.label(
            mandatory = False,
            executable = True,
            providers = ["files_to_run"],
            cfg = "host",
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
        deps,
        plugin = None,
        **kwargs):
    """Generate python code for a protobuf.

    Args:
      name: The name of the target.
      deps: A list of proto_library dependencies. Must contain a single element.
      plugin: An optional custom protoc plugin to execute together with
        generating the protobuf code.
      **kwargs: Additional arguments to be supplied to the invocation of
        py_library.
    """
    codegen_target = "_{}_codegen".format(name)
    if len(deps) != 1:
        fail("Can only compile a single proto at a time.")

    _generate_pb2_src(
        name = codegen_target,
        deps = deps,
        plugin = plugin,
        **kwargs
    )

    native.py_library(
        name = name,
        srcs = [":{}".format(codegen_target)],
        deps = [
            "@com_google_protobuf//:protobuf_python",
            ":{}".format(codegen_target),
        ],
        **kwargs
    )

def _generate_pb2_grpc_src_impl(context):
    protos = protos_from_context(context)
    includes = includes_from_deps(context.attr.deps)
    out_files = declare_out_files(protos, context, _GENERATED_GRPC_PROTO_FORMAT)

    plugin_flags = ["grpc_2_0"] + context.attr.strip_prefixes

    arguments = []
    tools = [context.executable._protoc, context.executable._grpc_plugin]
    out_dir = get_out_dir(protos, context)
    arguments += get_plugin_args(
        context.executable._grpc_plugin,
        plugin_flags,
        out_dir.path,
        False,
    )
    if context.attr.plugin:
        arguments += get_plugin_args(
            context.executable.plugin,
            [],
            out_dir.path,
            False,
            context.attr.plugin.label.name,
        )
        tools.append(context.executable.plugin)

    arguments += [
        "--proto_path={}".format(get_include_directory(i))
        for i in includes
    ]
    arguments += ["--proto_path={}".format(context.genfiles_dir.path)]
    arguments += get_proto_arguments(protos, context.genfiles_dir.path)

    context.actions.run(
        inputs = protos + includes,
        tools = tools,
        outputs = out_files,
        executable = context.executable._protoc,
        arguments = arguments,
        mnemonic = "ProtocInvocation",
    )

    return [
        DefaultInfo(files = depset(direct = out_files)),
        PyInfo(
            transitive_sources = depset(),
            # Imports are already configured by the generated py impl
            imports = depset(),
        ),
    ]

_generate_pb2_grpc_src = rule(
    attrs = {
        "deps": attr.label_list(
            mandatory = True,
            allow_empty = False,
            providers = [ProtoInfo],
        ),
        "strip_prefixes": attr.string_list(),
        "plugin": attr.label(
            mandatory = False,
            executable = True,
            providers = ["files_to_run"],
            cfg = "host",
        ),
        "_grpc_plugin": attr.label(
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
        plugin = None,
        strip_prefixes = [],
        **kwargs):
    """Generate python code for gRPC services defined in a protobuf.

    Args:
      name: The name of the target.
      srcs: (List of `labels`) a single proto_library target containing the
        schema of the service.
      deps: (List of `labels`) a single py_proto_library target for the
        proto_library in `srcs`.
      strip_prefixes: (List of `strings`) If provided, this prefix will be
        stripped from the beginning of foo_pb2 modules imported by the
        generated stubs. This is useful in combination with the `imports`
        attribute of the `py_library` rule.
      plugin: An optional custom protoc plugin to execute together with
        generating the gRPC code.
      **kwargs: Additional arguments to be supplied to the invocation of
        py_library.
    """
    codegen_grpc_target = "_{}_grpc_codegen".format(name)
    if len(srcs) != 1:
        fail("Can only compile a single proto at a time.")

    if len(deps) != 1:
        fail("Deps must have length 1.")

    _generate_pb2_grpc_src(
        name = codegen_grpc_target,
        deps = srcs,
        strip_prefixes = strip_prefixes,
        plugin = plugin,
        **kwargs
    )

    native.py_library(
        name = name,
        srcs = [
            ":{}".format(codegen_grpc_target),
        ],
        deps = [
            Label("//src/python/grpcio/grpc:grpcio"),
        ] + deps + [
            ":{}".format(codegen_grpc_target),
        ],
        **kwargs
    )

def py2and3_test(
        name,
        py_test = native.py_test,
        **kwargs):
    """Runs a Python test under both Python 2 and Python 3.

    Args:
      name: The name of the test.
      py_test: The rule to use for each test.
      **kwargs: Keyword arguments passed directly to the underlying py_test
        rule.
    """
    if "python_version" in kwargs:
        fail("Cannot specify 'python_version' in py2and3_test.")

    names = [name + suffix for suffix in (".python2", ".python3")]
    python_versions = ["PY2", "PY3"]
    for case_name, python_version in zip(names, python_versions):
        py_test(
            name = case_name,
            python_version = python_version,
            **kwargs
        )

    suite_kwargs = {}
    if "visibility" in kwargs:
        suite_kwargs["visibility"] = kwargs["visibility"]

    native.test_suite(
        name = name,
        tests = names,
        **suite_kwargs
    )
