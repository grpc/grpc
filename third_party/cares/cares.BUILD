load("@bazel_skylib//rules:copy_file.bzl", "copy_file")

config_setting(
    name = "darwin",
    values = {"cpu": "darwin"},
)

config_setting(
    name = "darwin_x86_64",
    values = {"cpu": "darwin_x86_64"},
)

config_setting(
    name = "darwin_arm64",
    values = {"cpu": "darwin_arm64"},
)

config_setting(
    name = "darwin_arm64e",
    values = {"cpu": "darwin_arm64e"},
)

config_setting(
    name = "windows",
    values = {"cpu": "x64_windows"},
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

config_setting(
    name = "ios_sim_arm64",
    values = {"cpu": "ios_sim_arm64"},
)

# The following architectures are found in 
# https://github.com/bazelbuild/bazel/blob/master/src/main/java/com/google/devtools/build/lib/rules/apple/ApplePlatform.java
config_setting(
    name = "tvos_x86_64",
    values = {"cpu": "tvos_x86_64"},
)

config_setting(
    name = "tvos_arm64",
    values = {"cpu": "tvos_arm64"}
)

config_setting(
    name = "watchos_i386",
    values = {"cpu": "watchos_i386"},
)

config_setting(
    name = "watchos_x86_64",
    values = {"cpu": "watchos_x86_64"}
)

config_setting(
    name = "watchos_armv7k",
    values = {"cpu": "watchos_armv7k"},
)

config_setting(
    name = "watchos_arm64_32",
    values = {"cpu": "watchos_arm64_32"}
)

config_setting(
    name = "openbsd",
    values = {"cpu": "openbsd"},
)

copy_file(
    name = "ares_build_h",
    src = "@com_github_grpc_grpc//third_party/cares:ares_build.h",
    out = "ares_build.h",
)

copy_file(
    name = "ares_config_h",
    src = select({
        ":ios_x86_64": "@com_github_grpc_grpc//third_party/cares:config_darwin/ares_config.h",
        ":ios_armv7": "@com_github_grpc_grpc//third_party/cares:config_darwin/ares_config.h",
        ":ios_armv7s": "@com_github_grpc_grpc//third_party/cares:config_darwin/ares_config.h",
        ":ios_arm64": "@com_github_grpc_grpc//third_party/cares:config_darwin/ares_config.h",
        ":ios_sim_arm64": "@com_github_grpc_grpc//third_party/cares:config_darwin/ares_config.h",
        ":tvos_x86_64": "@com_github_grpc_grpc//third_party/cares:config_darwin/ares_config.h",
        ":tvos_arm64": "@com_github_grpc_grpc//third_party/cares:config_darwin/ares_config.h",
        ":watchos_i386": "@com_github_grpc_grpc//third_party/cares:config_darwin/ares_config.h",
        ":watchos_x86_64": "@com_github_grpc_grpc//third_party/cares:config_darwin/ares_config.h",
        ":watchos_armv7k": "@com_github_grpc_grpc//third_party/cares:config_darwin/ares_config.h",
        ":watchos_arm64_32": "@com_github_grpc_grpc//third_party/cares:config_darwin/ares_config.h",
        ":darwin": "@com_github_grpc_grpc//third_party/cares:config_darwin/ares_config.h",
        ":darwin_x86_64": "@com_github_grpc_grpc//third_party/cares:config_darwin/ares_config.h",
        ":darwin_arm64": "@com_github_grpc_grpc//third_party/cares:config_darwin/ares_config.h",
        ":darwin_arm64e": "@com_github_grpc_grpc//third_party/cares:config_darwin/ares_config.h",
        ":windows": "@com_github_grpc_grpc//third_party/cares:config_windows/ares_config.h",
        ":android": "@com_github_grpc_grpc//third_party/cares:config_android/ares_config.h",
        ":openbsd": "@com_github_grpc_grpc//third_party/cares:config_openbsd/ares_config.h",
        "//conditions:default": "@com_github_grpc_grpc//third_party/cares:config_linux/ares_config.h",
    }),
    out = "ares_config.h",
)

cc_library(
    name = "ares",
    srcs = [
        "src/lib/ares__read_line.c",
        "src/lib/ares__get_hostent.c",
        "src/lib/ares__close_sockets.c",
        "src/lib/ares__timeval.c",
        "src/lib/ares_gethostbyaddr.c",
        "src/lib/ares_getenv.c",
        "src/lib/ares_free_string.c",
        "src/lib/ares_free_hostent.c",
        "src/lib/ares_fds.c",
        "src/lib/ares_expand_string.c",
        "src/lib/ares_create_query.c",
        "src/lib/ares_cancel.c",
        "src/lib/ares_android.c",
        "src/lib/ares_parse_txt_reply.c",
        "src/lib/ares_parse_srv_reply.c",
        "src/lib/ares_parse_soa_reply.c",
        "src/lib/ares_parse_ptr_reply.c",
        "src/lib/ares_parse_ns_reply.c",
        "src/lib/ares_parse_naptr_reply.c",
        "src/lib/ares_parse_mx_reply.c",
        "src/lib/ares_parse_caa_reply.c",
        "src/lib/ares_options.c",
        "src/lib/ares_nowarn.c",
        "src/lib/ares_mkquery.c",
        "src/lib/ares_llist.c",
        "src/lib/ares_getsock.c",
        "src/lib/ares_getnameinfo.c",
        "src/lib/bitncmp.c",
        "src/lib/ares_writev.c",
        "src/lib/ares_version.c",
        "src/lib/ares_timeout.c",
        "src/lib/ares_strerror.c",
        "src/lib/ares_strcasecmp.c",
        "src/lib/ares_search.c",
        "src/lib/ares_platform.c",
        "src/lib/windows_port.c",
        "src/lib/inet_ntop.c",
        "src/lib/ares__sortaddrinfo.c",
        "src/lib/ares__readaddrinfo.c",
        "src/lib/ares_parse_uri_reply.c",
        "src/lib/ares__parse_into_addrinfo.c",
        "src/lib/ares_parse_a_reply.c",
        "src/lib/ares_parse_aaaa_reply.c",
        "src/lib/ares_library_init.c",
        "src/lib/ares_init.c",
        "src/lib/ares_gethostbyname.c",
        "src/lib/ares_getaddrinfo.c",
        "src/lib/ares_freeaddrinfo.c",
        "src/lib/ares_expand_name.c",
        "src/lib/ares_destroy.c",
        "src/lib/ares_data.c",
        "src/lib/ares__addrinfo_localhost.c",
        "src/lib/ares__addrinfo2hostent.c",
        "src/lib/inet_net_pton.c",
        "src/lib/ares_strsplit.c",
        "src/lib/ares_strdup.c",
        "src/lib/ares_send.c",
        "src/lib/ares_rand.c",
        "src/lib/ares_query.c",
        "src/lib/ares_process.c",
    ],
    hdrs = [
        "ares_build.h",
        "ares_config.h",
        "include/ares_version.h",
        "include/ares.h",
        "include/ares_rules.h",
        "include/ares_dns.h",
        "include/ares_nameser.h",
        "src/tools/ares_getopt.h",
        "src/lib/ares_strsplit.h",
        "src/lib/ares_android.h",
        "src/lib/ares_private.h",
        "src/lib/ares_llist.h",
        "src/lib/ares_platform.h",
        "src/lib/ares_ipv6.h",
        "src/lib/config-dos.h",
        "src/lib/bitncmp.h",
        "src/lib/ares_strcasecmp.h",
        "src/lib/setup_once.h",
        "src/lib/ares_inet_net_pton.h",
        "src/lib/ares_data.h",
        "src/lib/ares_getenv.h",
        "src/lib/config-win32.h",
        "src/lib/ares_strdup.h",
        "src/lib/ares_iphlpapi.h",
        "src/lib/ares_setup.h",
        "src/lib/ares_writev.h",
        "src/lib/ares_nowarn.h",
    ],
    copts = [
        "-D_GNU_SOURCE",
        "-D_HAS_EXCEPTIONS=0",
        "-DHAVE_CONFIG_H",
    ] + select({
        ":windows": [
            "-DNOMINMAX",
            "-D_CRT_SECURE_NO_DEPRECATE",
            "-D_CRT_NONSTDC_NO_DEPRECATE",
            "-D_WIN32_WINNT=0x0600",
        ],
        "//conditions:default": [],
    }),
    defines = ["CARES_STATICLIB"],
    includes = ["include", "."],
    linkopts = select({
        ":windows": ["-defaultlib:ws2_32.lib"],
        "//conditions:default": [],
    }),
    linkstatic = 1,
    visibility = [
        "//visibility:public",
    ],
    alwayslink = 1,
)
