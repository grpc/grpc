def grpc_cc_library(name, srcs = [], hdrs = [], deps = [], standalone = False, language = "C++"):
  copts = []
  if language == "C":
    copts = ["-std=c99"]
  native.cc_library(
    name = name,
    srcs = srcs,
    hdrs = hdrs,
    deps = deps,
    copts = copts,
    linkopts = ["-pthread"],
    includes = [
        "include"
    ]
  )


def nanopb():
  native.cc_library(
    name = "nanopb",
    srcs = [
      '//third_party/nanopb/pb_common.c',
      '//third_party/nanopb/pb_decode.c',
      '//third_party/nanopb/pb_encode.c',
    ],
    hdrs = [
      '//third_party/nanopb/pb.h',
      '//third_party/nanopb/pb_common.h',
      '//third_party/nanopb/pb_decode.h',
      '//third_party/nanopb/pb_encode.h',
    ]
  )
