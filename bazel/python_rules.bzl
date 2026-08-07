# Copyright 2021 The gRPC Authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""Generates and compiles Python gRPC stubs from proto_library rules."""

load("@com_google_protobuf//bazel:py_proto_library.bzl", protobuf_py_proto_library = "py_proto_library")
load("@rules_proto//proto:defs.bzl", "ProtoInfo")
load("@rules_python//python:py_info.bzl", "PyInfo")
load(
    "//bazel:protobuf.bzl",
    "declare_out_files",
    "get_include_directory",
    "get_out_dir",
    "get_plugin_args",
    "get_proto_arguments",
    "get_staged_proto_file",
    "includes_from_deps",
    "is_well_known",
    "protos_from_context",
)

_VIRTUAL_IMPORTS = "/_virtual_imports/"
_GENERATED_PROTO_FORMAT = "{}_pb2.py"
_GENERATED_PROTO_STUB_FORMAT = "{}_pb2.pyi"
_GENERATED_GRPC_PROTO_FORMAT = "{}_pb2_grpc.py"

def _merge_pyinfos(pyinfos):
    return PyInfo(
        transitive_sources = depset(transitive = [p.transitive_sources for p in pyinfos]),
        imports = depset(transitive = [p.imports for p in pyinfos]),
    )

def py_proto_library(name, deps, **kwargs):
    """Use `py_proto_library` to generate Python libraries from `.proto` files.

    Args:
      name: The name of the target.
      deps: A single proto_library target.
      **kwargs: Additional arguments to be supplied to the invocation of
        py_library.
    """
    filtered_kwargs = {k: v for k, v in kwargs.items() if k not in ("imports", "use_protobuf")}
    protobuf_py_proto_library(name = name, deps = deps, **filtered_kwargs)

def _generate_pb2_grpc_src_impl(context):
    protos = protos_from_context(context)
    includes = includes_from_deps(context.attr.deps)

    out_files = declare_out_files(protos, context, _GENERATED_GRPC_PROTO_FORMAT)
    plugin_flags = ["grpc_2_0"] + context.attr.strip_prefixes

    arguments = []
    tools = [context.executable._protoc, context.executable._grpc_plugin]
    out_dir = get_out_dir(protos, context)
    if out_dir.import_path:
        # is virtual imports
        out_path = get_include_directory(out_files[0])
    else:
        out_path = out_dir.path
    arguments += get_plugin_args(
        context.executable._grpc_plugin,
        plugin_flags,
        out_path,
        False,
    )

    arguments += [
        "--proto_path={}".format(get_include_directory(i))
        for i in includes
    ]
    arguments.append("--proto_path={}".format(context.genfiles_dir.path))
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

    # Adding to PYTHONPATH so the generated modules can be imported.
    # This is necessary when there is strip_import_prefix, the Python modules are generated under _virtual_imports.
    # But it's undesirable otherwise, because it will put the repo root at the top of the PYTHONPATH, ahead of
    # directories added through `imports` attributes.
    if _VIRTUAL_IMPORTS in out_path:
        import_path = out_path

        # Handles virtual import cases
        if out_path.startswith(context.genfiles_dir.path):
            import_path = import_path[len(context.genfiles_dir.path) + 1:]

        import_path = "{}/{}".format(context.workspace_name, import_path)
        imports.append(import_path)

    p = PyInfo(transitive_sources = depset(direct = out_files), imports = depset(direct = imports))
    py_info = _merge_pyinfos(
        [
            p,
            context.attr.grpc_library[PyInfo],
        ] + [dep[PyInfo] for dep in context.attr.py_deps],
    )

    runfiles = context.runfiles(files = out_files, transitive_files = py_info.transitive_sources).merge(context.attr.grpc_library[DefaultInfo].data_runfiles)

    return [
        DefaultInfo(
            files = depset(direct = out_files),
            runfiles = runfiles,
        ),
        py_info,
    ]

_generate_pb2_grpc_src = rule(
    attrs = {
        "deps": attr.label_list(
            mandatory = True,
            allow_empty = False,
            providers = [ProtoInfo],
        ),
        "py_deps": attr.label_list(
            mandatory = True,
            allow_empty = False,
            providers = [PyInfo],
        ),
        "strip_prefixes": attr.string_list(),
        "_grpc_plugin": attr.label(
            executable = True,
            cfg = "exec",
            default = Label("//src/compiler:grpc_python_plugin"),
        ),
        "_protoc": attr.label(
            executable = True,
            cfg = "exec",
            default = Label("@com_google_protobuf//:protoc"),
        ),
        "grpc_library": attr.label(
            default = Label("//src/python/grpcio/grpc:grpcio"),
            providers = [PyInfo],
        ),
    },
    implementation = _generate_pb2_grpc_src_impl,
)

def py_grpc_library(
        name,
        srcs,
        deps,
        strip_prefixes = [],
        grpc_library = Label("//src/python/grpcio/grpc:grpcio"),
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
      grpc_library: (`label`) a single `py_library` target representing the
        python gRPC library target to be depended upon. This can be used to
        generate code that depends on `grpcio` from the Python Package Index.
      **kwargs: Additional arguments to be supplied to the invocation of
        py_library.
    """
    if len(srcs) != 1:
        fail("Can only compile a single proto at a time.")

    if len(deps) != 1:
        fail("Deps must have length 1.")

    _generate_pb2_grpc_src(
        name = name,
        deps = srcs,
        py_deps = deps,
        strip_prefixes = strip_prefixes,
        grpc_library = grpc_library,
        **kwargs
    )
