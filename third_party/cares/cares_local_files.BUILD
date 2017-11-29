package(
    default_visibility = ["//visibility:public"],
)

config_setting(
    name = "darwin",
    values = {"cpu": "darwin"},
)

# Android is not officially supported through C++.
# This just helps with the build for now.
config_setting(
    name = "android",
    values = {
        "crosstool_top": "//external:android/crosstool",
    },
)

# iOS is not officially supported through C++.
# This just helps with the build for now.
config_setting(
    name = "ios_x86_64",
    values = {"cpu": "ios_x86_64"},
)

config_setting(
    name = "ios_armv7",
    values = {"cpu": "ios_armv7"},
)

config_setting(
    name = "ios_armv7s",
    values = {"cpu": "ios_armv7s"},
)

config_setting(
    name = "ios_arm64",
    values = {"cpu": "ios_arm64"},
)

filegroup(
    name = "ares_build_h",
    srcs = ["ares_build.h"],
)

filegroup(
    name = "ares_config_h",
    srcs = select({
        ":ios_x86_64": ["config_darwin/ares_config.h"],
        ":ios_armv7": ["config_darwin/ares_config.h"],
        ":ios_armv7s": ["config_darwin/ares_config.h"],
        ":ios_arm64": ["config_darwin/ares_config.h"],
        ":darwin": ["config_darwin/ares_config.h"],
        ":android": ["config_android/ares_config.h"],
        "//conditions:default": ["config_linux/ares_config.h"],
    }),
)
