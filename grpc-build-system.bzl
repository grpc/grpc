#
# This is for the gRPC build system. This isn't intended to be used outsite of
# the BUILD file for gRPC. It contains the mapping for the template system we
# use to generate other platform's build system files.
#

def grpc_cc_library(name, srcs = [], public_hdrs = [], hdrs = [], external_deps = [], deps = [], standalone = False, language = "C++"):
  copts = []
  if language == "C":
    copts = ["-std=c99"]
  native.cc_library(
    name = name,
    srcs = srcs,
    hdrs = hdrs + public_hdrs,
    deps = deps + ["//external:" + dep for dep in external_deps],
    copts = copts,
    linkopts = ["-pthread"],
    includes = [
        "include"
    ]
  )
