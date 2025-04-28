cc_binary(
     name = "fd_server",
     srcs = ["fd_server.cc"],
     defines = ["BAZEL_BUILD"],
     deps = [
         "//:grpc++",
         "//examples/protos:helloworld_cc_grpc",
         "@com_google_absl//absl/flags:parse",
         "@com_google_absl//absl/log:initialize",
     ],
)

cc_binary(
     name = "fd_client",
     srcs = ["fd_client.cc"],
     defines = ["BAZEL_BUILD"],
     deps = [
         "//:grpc++",
         "//examples/protos:helloworld_cc_grpc",
         "@com_google_absl//absl/flags:parse",
         "@com_google_absl//absl/log:initialize",
    ],
)

cc_binary(
     name = "secure_fd_client",
     srcs = ["secure_fd_client.cc"],
     defines = ["BAZEL_BUILD"],
     deps = [
         "//:grpc++",
         "//examples/protos:helloworld_cc_grpc",
         "@com_google_absl//absl/flags:parse",
         "@com_google_absl//absl/log:initialize",
    ],
)

cc_binary(
     name = "passive_server",
     srcs = ["passive_server.cc"],
     defines = ["BAZEL_BUILD"],
     deps = [
         "//:grpc++",
         "//examples/protos:helloworld_cc_grpc",
         "@com_google_absl//absl/flags:parse",
         "@com_google_absl//absl/log:initialize",
    ],
)
