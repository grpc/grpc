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
  includes = ["."],
  deps = ["//:grpc"]
)

objc_library(
  name = "grpc_protoRPC",
  srcs = glob(["ProtoRPC/*.m"]),
  hdrs = glob(["ProtoRPC/*.h"]),
  defines = ["GPB_USE_PROTOBUF_FRAMEWORK_IMPORTS=0"],
  includes = [
    ".",
    "../../third_party/protobuf/objectivec"
  ],
  deps = [
    "//:grpc",
    "//src/objective-c:grpc_objc",
    "//third_party/protobuf:protobuf_objc"
  ]
)


