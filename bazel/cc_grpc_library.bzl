"""Generates and compiles C++ grpc stubs from proto_library rules."""

load("//:bazel/generate_cc.bzl", "generate_cc")

def cc_grpc_library(name, srcs, deps, proto_only, well_known_protos, generate_mock, use_external = False, **kwargs):
  """Generates C++ grpc classes from a .proto file.

  Assumes the generated classes will be used in cc_api_version = 2.

  Arguments:
      name: name of rule.
      srcs: a single proto_library, which wraps the .proto files with services.
      deps: a list of C++ proto_library (or cc_proto_library) which provides
        the compiled code of any message that the services depend on.
      well_known_protos: Should this library additionally depend on well known
        protos
      use_external: When True the grpc deps are prefixed with //external. This
        allows grpc to be used as a dependency in other bazel projects.
      generate_mock: When true GMOCk code for client stub is generated.
      **kwargs: rest of arguments, e.g., compatible_with and visibility.
  """
  if len(srcs) > 1:
    fail("Only one srcs value supported", "srcs")

  proto_target = "_" + name + "_only"
  codegen_target = "_" + name + "_codegen"
  codegen_grpc_target = "_" + name + "_grpc_codegen"
  proto_deps = ["_" + dep + "_only" for dep in deps if dep.find(':') == -1]
  proto_deps += [dep.split(':')[0] + ':' + "_" + dep.split(':')[1] + "_only" for dep in deps if dep.find(':') != -1]

  native.proto_library(
      name = proto_target,
      srcs = srcs,
      deps = proto_deps,
      **kwargs
  )

  generate_cc(
      name = codegen_target,
      srcs = [proto_target],
      well_known_protos = well_known_protos,
      **kwargs
  )

  if not proto_only:
    if use_external:
      # when this file is used by non-grpc projects
      plugin = "//external:grpc_cpp_plugin"
    else:
      plugin = "//:grpc_cpp_plugin"

    generate_cc(
        name = codegen_grpc_target,
        srcs = [proto_target],
        plugin = plugin,
        well_known_protos = well_known_protos,
        generate_mock = generate_mock,
        **kwargs
    )

    if use_external:
      # when this file is used by non-grpc projects
      grpc_deps = ["//external:grpc++", "//external:grpc++_codegen_proto",
                   "//external:protobuf"]
    else:
      grpc_deps = ["//:grpc++", "//:grpc++_codegen_proto", "//external:protobuf"]

    native.cc_library(
        name = name,
        srcs = [":" + codegen_grpc_target, ":" + codegen_target],
        hdrs = [":" + codegen_grpc_target, ":" + codegen_target],
        deps = deps + grpc_deps,
        **kwargs
    )
  else:
    native.cc_library(
        name = name,
        srcs = [":" + codegen_target],
        hdrs = [":" + codegen_target],
        deps = deps + ["//external:protobuf"],
        **kwargs
    )
