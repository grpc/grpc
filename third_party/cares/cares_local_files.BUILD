package(
    default_visibility = ["//visibility:public"],
)

config_setting(
    name = "darwin",
    values = {"cpu": "darwin"},
)

filegroup(
    name = "ares_build_h",
    srcs = ["ares_build.h"],
)

filegroup(
    name = "ares_config_h",
    srcs = select({
        ":darwin": ["config_darwin/ares_config.h"],
        "//conditions:default": ["config_linux/ares_config.h"],
    }),
)
