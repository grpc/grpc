"""Generates C++ grpc stubs from proto_library rules.

This is an internal rule used by cc_grpc_library, and shouldn't be used
directly.
"""

def generate_cc_impl(ctx):
  """Implementation of the generate_cc rule."""
  protos = [f for src in ctx.attr.srcs for f in src.proto.direct_sources]
  includes = [f for src in ctx.attr.srcs for f in src.proto.transitive_imports]
  outs = []
  if ctx.executable.plugin:
    outs += [proto.basename[:-len(".proto")] + ".grpc.pb.h" for proto in protos]
    outs += [proto.basename[:-len(".proto")] + ".grpc.pb.cc" for proto in protos]
  else:
    outs += [proto.basename[:-len(".proto")] + ".pb.h" for proto in protos]
    outs += [proto.basename[:-len(".proto")] + ".pb.cc" for proto in protos]
  out_files = [ctx.new_file(out) for out in outs]
  # The following should be replaced with ctx.configuration.buildout
  # whenever this is added to Skylark.
  dir_out = out_files[0].dirname[:-len(protos[0].dirname)]

  arguments = []
  if ctx.executable.plugin:
    arguments += ["--plugin=protoc-gen-PLUGIN=" + ctx.executable.plugin.path]
    arguments += ["--PLUGIN_out=" + ",".join(ctx.attr.flags) + ":" + dir_out]
    additional_input = [ctx.executable.plugin]
  else:
    arguments += ["--cpp_out=" + ",".join(ctx.attr.flags) + ":" + dir_out]
    additional_input = []
  arguments += ["-I{0}={0}".format(include.path) for include in includes]
  arguments += [proto.path for proto in protos]

  # create a list of well known proto files if the argument is non-None
  well_known_proto_files = []
  if ctx.attr.well_known_protos:
    f = ctx.attr.well_known_protos.files.to_list()[0].dirname
    if f != "external/submodule_protobuf/src/google/protobuf":
      print("Error: Only @submodule_protobuf//:well_known_protos is supported")
    else:
      # f points to "external/submodule_protobuf/src/google/protobuf"
      # add -I argument to protoc so it knows where to look for the proto files.
      arguments += ["-I{0}".format(f + "/../..")]
      well_known_proto_files = [f for f in ctx.attr.well_known_protos.files]

  ctx.action(
      inputs = protos + includes + additional_input + well_known_proto_files,
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
            cfg = "host",
        ),
        "flags": attr.string_list(
            mandatory = False,
            allow_empty = True,
        ),
        "well_known_protos" : attr.label(
            mandatory = False,
        ),
        "_protoc": attr.label(
            default = Label("//external:protocol_compiler"),
            executable = True,
            cfg = "host",
        ),
    },
    # We generate .h files, so we need to output to genfiles.
    output_to_genfiles = True,
    implementation = generate_cc_impl,
)
