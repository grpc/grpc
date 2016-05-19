
cc_library(
    name = "helloworld_proto",
    hdrs = [
        "helloworld.pb.h",
        "helloworld.grpc.pb.h",
    ],
    srcs = [
        "helloworld.pb.cc",
        "helloworld.grpc.pb.cc",
    ],
    deps = [
        "//external:grpc++",
    ],
)

cc_binary(
    name = "greeter_client",
    srcs = [
        "greeter_client.cc",
    ],
    deps = [
        ":helloworld_proto",
        "//external:grpc++",
    ],
)

cc_binary(
    name = "greeter_server",
    srcs = [
        "greeter_server.cc",
    ],
    deps = [
        ":helloworld_proto",
        "//external:grpc++",
    ],
)
