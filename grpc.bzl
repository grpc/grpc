
def _lower_underscore_to_upper_camel(str):
  humps = []
  for hump in str.split('_'):
    humps += [hump[0].upper() + hump[1:]]
  return "".join(humps)

def objc_grpc_library(name, srcs, visibility=None):
  src_path_elements = srcs[0].split('/')
  src_dir = '/'.join(src_path_elements[:-1])
  basename = src_path_elements[-1]
  filename = basename[:-6] # remove .proto suffix
  filename = _lower_underscore_to_upper_camel(filename)

  protoc_command = "protoc -I . "
  srcs_params = ""
  for src in srcs:
    srcs_params += " $(location %s)" % (src)

  # Messages
  protoc_messages_flags = "--objc_out=$(GENDIR)"
  message_header = src_dir + '/' + filename + ".pbobjc.h"
  message_implementation = src_dir + '/' + filename + ".pbobjc.m"
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
  service_header = src_dir + '/' + filename + ".pbrpc.h"
  service_implementation = src_dir + '/' + filename + ".pbrpc.m"
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
