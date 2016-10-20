"""Generates and compiles C++ grpc stubs from proto_library rules."""

load("//:bazel/generate_cc.bzl", "generate_cc")

def cc_grpc_library(name, srcs, deps, **kwargs):
  """Generates C++ grpc classes from a .proto file.

  Assumes the generated classes will be used in cc_api_version = 2.

  Arguments:
      name: name of rule.
      srcs: a single proto_library, which wraps the .proto files with services.
      deps: a list of C++ proto_library (or cc_proto_library) which provides
        the compiled code of any message that the services depend on.
      **kwargs: rest of arguments, e.g., compatible_with and visibility.
  """
  if len(srcs) > 1:
    fail("Only one srcs value supported", "srcs")

  codegen_target = "_" + name + "_codegen"

  generate_cc(
      name = codegen_target,
      srcs = srcs,
      plugin = "//external:grpc_cpp_plugin",
      **kwargs
  )

  native.cc_library(
      name = name,
      srcs = [":" + codegen_target],
      hdrs = [":" + codegen_target],
      deps = deps + ["//external:grpc++"],
      **kwargs
  )
