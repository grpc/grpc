objc_library(
  name = "grpc_objc",
  srcs = glob([
      "GRPCClient/*.m", "GRPCClient/**/*.m", 
      "RxLibrary/*.m", "RxLibrary/**/*.m"
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
  copts = [
    "-Isrc/objective-c",
    "-Ithird_party",
    "-Ithird_party/protobuf/objectivec/"
  ],
  deps = [
    "//src/objective-c:grpc_objc",
    "//:grpc",
  ]
)