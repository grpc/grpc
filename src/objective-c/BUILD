load("@build_bazel_rules_apple//apple:ios.bzl", "ios_application")

objc_library(
  name = "grpc_objc",
  srcs = glob([
      "GRPCClient/**/*.m", 
      "RxLibrary/**/*.m"
    ], 
    exclude = ["GRPCClient/GRPCCall+GID.m"]
  ),
  hdrs = glob([
      "GRPCClient/**/*.h", 
      "RxLibrary/**/*.h"
    ], 
    exclude = ["GRPCClient/GRPCCall+GID.h"]
  ),
  includes = ["."],
  sdk_frameworks = ["SystemConfiguration"],
  deps = ["//:grpc"]
)

objc_library(
  name = "grpc_protoRPC",
  srcs = glob(["ProtoRPC/*.m"]),
  hdrs = glob(["ProtoRPC/*.h"]),
  defines = ["GPB_USE_PROTOBUF_FRAMEWORK_IMPORTS=0"],
  includes = [
    ".",
    "@protobuf/objectivec"
  ],
  deps = [
    "//:grpc",
    ":grpc_objc",
    "//third_party/protobuf:protobuf_objc"
  ]
)

# cc_binary(
#   name = "protoCompiler_grpc_objective_c_plugin",
#   srcs = ["//:protoCompiler_grpc_objective_c_plugin_archive"],
# )

# genrule(
#   name = "compileProto",
#   srcs = [
#     "RemoteTest/messages.proto",
#     "@com_google_protobuf//:protoc",
#     "grpc_objective_c_plugin",
#   ],
#   cmd = "$(location @com_google_protobuf//:protoc) \
#     --plugin=protoc-gen-grpc=$(location grpc_objective_c_plugin)\
#     --objc_out=src/objective-c/RemoteTest --grpc_out=src/objective-c/RemoteTest -I . \
#     $(location RemoteTest/messages.proto)",
#   outs = [
#     "RemoteTest/Messages.pbobjc.h",
#     "RemoteTest/Messages.phobjc.m"
#   ]
# )

objc_library(
  name = "remoteTest",
  non_arc_srcs = glob(["RemoteTest/*.m"]),
  hdrs = glob(["RemoteTest/*.h"]),
  defines = [
    "GPB_USE_PROTOBUF_FRAMEWORK_IMPORTS=0",
    "GPB_GRPC_FORWARD_DECLARE_MESSAGE_PROTO=0"
  ],
  includes = ["."],
  deps = [":grpc_protoRPC"]
)

objc_library(
  name = "bazelTestObjc",
  srcs = glob(["bazelTest/bazelTest/**/*.m"]),
  hdrs = glob(["bazelTest/bazelTest/**/*.h"]),
  data = glob(["bazelTest/bazelTest/Assets.xcassets/*"]) + [
    "bazelTest/bazelTest/Base.lproj/LaunchScreen.storyboard",
    "bazelTest/bazelTest/Base.lproj/Main.storyboard"
  ],
  includes = ["."],
  deps = [":remoteTest"]
)

ios_application(
  name = "bazelTest",
  bundle_id = "com.google.bazelObjCTest",
  families = ["iphone"],
  minimum_os_version = "9.0",
  infoplists = [":bazelTest/bazelTest/Info.plist"],
  visibility = ["//visibility:public"],
  deps = [":bazelTestObjc"],
)
