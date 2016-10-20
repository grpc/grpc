"""Generates C++ grpc stubs from proto_library rules.

This is an internal rule used by cc_grpc_library, and shouldn't be used
directly.
"""

def generate_cc_impl(ctx):
  """Implementation of the gengrpccc rule."""
  protos = [f for src in ctx.attr.srcs for f in src.proto.direct_sources]
  includes = [f for src in ctx.attr.srcs for f in src.proto.transitive_imports]
  outs = []
  outs += [proto.basename[:-len(".proto")] + ".grpc.pb.h" for proto in protos]
  outs += [proto.basename[:-len(".proto")] + ".grpc.pb.cc" for proto in protos]
  out_files = [ctx.new_file(out) for out in outs]
  # The following should be replaced with ctx.configuration.buildout
  # whenever this is added to Skylark.
  dir_out = out_files[0].dirname[:-len(protos[0].dirname)]

  arguments = []
  arguments += ["--plugin=protoc-gen-PLUGIN=" + ctx.executable.plugin.path]
  arguments += ["--PLUGIN_out=" + ",".join(ctx.attr.flags) + ":" + dir_out]
  arguments += ["-I{0}={0}".format(include.path) for include in includes]
  arguments += [proto.path for proto in protos]

  ctx.action(
      inputs = protos + includes,
      outputs = out_files,
      executable = ctx.executable._protoc,
      arguments = arguments,
  )

  return struct(files=set(out_files))

generate_cc = rule(
    attrs = {
        "srcs": attr.label_list(
            mandatory = True,
            non_empty = True,
            providers = ["proto"],
        ),
        "plugin": attr.label(
            executable = True,
            providers = ["files_to_run"],
            cfg = HOST_CFG,
        ),
        "flags": attr.string_list(
            mandatory = True,
            allow_empty = False
        ),
        "_protoc": attr.label(
            default = Label("//extern:protocol_compiler"),
            executable = True,
            cfg = HOST_CFG,
        ),
    },
    # We generate .h files, so we need to output to genfiles.
    output_to_genfiles = True,
    implementation = generate_cc_impl,
)
