package(default_visibility = ["//visibility:public"])

cc_library(
    name = "openssl",
    includes = ["."],
    linkopts = ["-L/usr/lib", "-lssl", "-lcrypto"],
    )
cc_library(
    name = "crypto",
    includes = ["."],
    linkopts = [
        "-L/usr/lib",
        "-lcrypto",  
    ],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "ssl",
    includes = ["."],
    linkopts = [
        "-L/usr/lib",
        "-lssl",  
    ],
        deps = [":crypto"],  
    visibility = ["//visibility:public"],
)
