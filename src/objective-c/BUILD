objc_library(
  name = "grpc_objc",
  srcs = glob([
      "GRPCClient/**/*.m", 
      "RxLibrary/**/*.m"
    ], 
    exclude = ["GRPCClient/GRPCCall+GID.m"]
  ),
  hdrs = glob([
      "GRPCClient/*.h", "GRPCClient/**/*.h", 
      "RxLibrary/*.h", "RxLibrary/**/*.h"
    ], 
    exclude = ["GRPCClient/GRPCCall+GID.h"]
  ),
  copts = [
    "-Isrc/objective-c"
  ],
  deps = ["//:grpc"]
)

objc_library(
  name = "grpc_protoRPC",
  srcs = glob(["ProtoRPC/*.m"]),
  hdrs = glob(["ProtoRPC/*.h"]),
  defines = ["GPB_USE_PROTOBUF_FRAMEWORK_IMPORTS=1"],
  copts = [
    "-Isrc/objective-c",
  ],
  deps = [
    "//src/objective-c:grpc_objc",
    "//:grpc",
    "//third_party/protobuf:protobuf_objc"
  ]
)