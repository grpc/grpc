"""Generates C++ grpc stubs from proto_library rules.

This is an internal rule used by cc_grpc_library, and shouldn't be used
directly.
"""

def generate_cc_impl(ctx):
  """Implementation of the generate_cc rule."""
  protos = [f for src in ctx.attr.srcs for f in src.proto.direct_sources]
  includes = [f for src in ctx.attr.srcs for f in src.proto.transitive_imports]
  outs = []
  # label_len is length of the package name rooted in the local repository.  It
  # does not include the external prefix if being built as an external target.
  label_len = len(ctx.label.package) + 1
  for proto in protos:
    label_offset = proto.path.index(ctx.label.package)
    path = proto.path[label_offset + label_len:-len(".proto")]
    if ctx.executable.plugin:
      outs += [path + ".grpc.pb.h", path + ".grpc.pb.cc"]
      if ctx.attr.generate_mock:
        outs += [path + "_mock.grpc.pb.h"]
    else:
      outs += [path + ".pb.h", path + ".pb.cc"]
  out_files = [ctx.new_file(out) for out in outs]
  dir_out = str(ctx.genfiles_dir.path)

  arguments = []
  if ctx.executable.plugin:
    arguments += ["--plugin=protoc-gen-PLUGIN=" + ctx.executable.plugin.path]
    flags = list(ctx.attr.flags)
    if ctx.attr.generate_mock:
      flags.append("generate_mock_code=true")
    arguments += ["--PLUGIN_out=" + ",".join(flags) + ":" + dir_out]
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
    if f != "external/com_google_protobuf/src/google/protobuf":
      print("Error: Only @com_google_protobuf//:well_known_protos is supported")
    else:
      # f points to "external/com_google_protobuf/src/google/protobuf"
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
        "generate_mock" : attr.bool(
            default = False,
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
