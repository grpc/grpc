
def _lower_underscore_to_upper_camel(str):
  humps = []
  for hump in str.split('_'):
    humps += [hump[0].upper() + hump[1:]]
  return "".join(humps)

def objc_grpc_library(name, srcs, visibility=None):
  basename = srcs[0].split('/')[-1]
  filename = basename[:-6] # remove .proto suffix
  filename = _lower_underscore_to_upper_camel(filename)

  protoc_command = "protoc -I . "
  srcs_params = ""
  for src in srcs:
    srcs_params += " $(location %s)" % (src)

  # Messages
  protoc_messages_flags = "--objc_out=$(GENDIR)"
  native.genrule(
    name = name + "_mesages_codegen",
    srcs = srcs,
    outs = [
      filename + ".pbobjc.h",
      filename + ".pbobjc.m",
    ],
    cmd = protoc_command + protoc_messages_flags + srcs_params,
  )
  native.objc_library(
    name = name + "_messages",
    hdrs = [
      ":" + filename + ".pbobjc.h",
    ],
    includes = ["."],
    non_arc_srcs = [
      ":" + filename + ".pbobjc.m",
    ],
    deps = [
      "//external:protobuf_objc",
    ],
  )

  # Services
  protoc_services_flags = "--grpc_out=$(GENDIR) --plugin=protoc-gen-grpc=$(location //external:grpc_protoc_plugin_objc)"
  native.genrule(
    name = name + "_codegen",
    srcs = srcs + ["//external:grpc_protoc_plugin_objc"],
    outs = [
      filename + ".pbrpc.h",
      filename + ".pbrpc.m",
    ],
    cmd = protoc_command + protoc_services_flags + srcs_params,
  )
  native.objc_library(
    name = name,
    hdrs = [
      ":" + filename + ".pbrpc.h",
    ],
    includes = ["."],
    srcs = [
      ":" + filename + ".pbrpc.m",
    ],
    deps = [
      ":" + name + "_messages",
      "//external:proto_objc_rpc",
    ],
    visibility = visibility,
  )
