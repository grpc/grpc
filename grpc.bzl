
def _lower_underscore_to_upper_camel(str):
  humps = []
  for hump in str.split('_'):
    humps += [hump[0].upper() + hump[1:]]
  return "".join(humps)

def _file_to_upper_camel(src):
  elements = src.rpartition('/')
  upper_camel = _lower_underscore_to_upper_camel(elements[-1])
  return "".join(elements[:-1] + [upper_camel])

def _file_with_extension(src, ext):
  elements = src.rpartition('/')
  basename = elements[-1].partition('.')[0]
  return "".join(elements[:-1] + [basename, ext])

def objc_grpc_library(name, srcs, visibility=None):
  protoc_command = "protoc -I . "
  srcs_params = ""
  for src in srcs:
    srcs_params += " $(location %s)" % (src)

  src = _file_to_upper_camel(srcs[0])

  # Messages
  protoc_messages_flags = "--objc_out=$(GENDIR)"
  message_header = _file_with_extension(src, ".pbobjc.h")
  message_implementation = _file_with_extension(src, ".pbobjc.m")
  native.genrule(
    name = name + "_mesages_codegen",
    srcs = srcs,
    outs = [message_header, message_implementation],
    cmd = protoc_command + protoc_messages_flags + srcs_params,
  )
  native.objc_library(
    name = name + "_messages",
    hdrs = [message_header],
    includes = ["."],
    non_arc_srcs = [message_implementation],
    deps = [
      "//external:protobuf_objc",
    ],
  )

  # Services
  protoc_services_flags = "--grpc_out=$(GENDIR) --plugin=protoc-gen-grpc=$(location //external:grpc_protoc_plugin_objc)"
  service_header = _file_with_extension(src, ".pbrpc.h")
  service_implementation = _file_with_extension(src, ".pbrpc.m")
  native.genrule(
    name = name + "_codegen",
    srcs = srcs + ["//external:grpc_protoc_plugin_objc"],
    outs = [service_header, service_implementation],
    cmd = protoc_command + protoc_services_flags + srcs_params,
  )
  native.objc_library(
    name = name,
    hdrs = [service_header],
    includes = ["."],
    srcs = [service_implementation],
    deps = [
      ":" + name + "_messages",
      "//external:proto_objc_rpc",
    ],
    visibility = visibility,
  )
