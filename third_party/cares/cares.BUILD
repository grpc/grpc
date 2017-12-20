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

genrule(
    name = "ares_build_h",
    srcs = ["@com_github_grpc_grpc//third_party/cares:ares_build.h"],
    outs = ["ares_build.h"],
    cmd = "cat $< > $@",
)

genrule(
    name = "ares_config_h",
    srcs = select({
        ":ios_x86_64": ["@com_github_grpc_grpc//third_party/cares:config_darwin/ares_config.h"],
        ":ios_armv7": ["@com_github_grpc_grpc//third_party/cares:config_darwin/ares_config.h"],
        ":ios_armv7s": ["@com_github_grpc_grpc//third_party/cares:config_darwin/ares_config.h"],
        ":ios_arm64": ["@com_github_grpc_grpc//third_party/cares:config_darwin/ares_config.h"],
        ":darwin": ["@com_github_grpc_grpc//third_party/cares:config_darwin/ares_config.h"],
        ":android": ["@com_github_grpc_grpc//third_party/cares:config_android/ares_config.h"],
        "//conditions:default": ["@com_github_grpc_grpc//third_party/cares:config_linux/ares_config.h"],
    }),
    outs = ["ares_config.h"],
    cmd = "cat $< > $@",
)

cc_library(
    name = "ares",
    srcs = [
        "ares__close_sockets.c",
        "ares__get_hostent.c",
        "ares__read_line.c",
        "ares__timeval.c",
        "ares_cancel.c",
        "ares_create_query.c",
        "ares_data.c",
        "ares_destroy.c",
        "ares_expand_name.c",
        "ares_expand_string.c",
        "ares_fds.c",
        "ares_free_hostent.c",
        "ares_free_string.c",
        "ares_getenv.c",
        "ares_gethostbyaddr.c",
        "ares_gethostbyname.c",
        "ares_getnameinfo.c",
        "ares_getopt.c",
        "ares_getsock.c",
        "ares_init.c",
        "ares_library_init.c",
        "ares_llist.c",
        "ares_mkquery.c",
        "ares_nowarn.c",
        "ares_options.c",
        "ares_parse_a_reply.c",
        "ares_parse_aaaa_reply.c",
        "ares_parse_mx_reply.c",
        "ares_parse_naptr_reply.c",
        "ares_parse_ns_reply.c",
        "ares_parse_ptr_reply.c",
        "ares_parse_soa_reply.c",
        "ares_parse_srv_reply.c",
        "ares_parse_txt_reply.c",
        "ares_platform.c",
        "ares_process.c",
        "ares_query.c",
        "ares_search.c",
        "ares_send.c",
        "ares_strcasecmp.c",
        "ares_strdup.c",
        "ares_strerror.c",
        "ares_timeout.c",
        "ares_version.c",
        "ares_writev.c",
        "bitncmp.c",
        "inet_net_pton.c",
        "inet_ntop.c",
        "windows_port.c",
    ],
    hdrs = [
        "ares.h",
        "ares_build.h",
        "ares_config.h",
        "ares_data.h",
        "ares_dns.h",
        "ares_getenv.h",
        "ares_getopt.h",
        "ares_inet_net_pton.h",
        "ares_iphlpapi.h",
        "ares_ipv6.h",
        "ares_library_init.h",
        "ares_llist.h",
        "ares_nowarn.h",
        "ares_platform.h",
        "ares_private.h",
        "ares_rules.h",
        "ares_setup.h",
        "ares_strcasecmp.h",
        "ares_strdup.h",
        "ares_version.h",
        "bitncmp.h",
        "config-win32.h",
        "nameser.h",
        "setup_once.h",
    ],
    copts = [
        "-D_GNU_SOURCE",
        "-D_HAS_EXCEPTIONS=0",
        "-DNOMINMAX",
        "-DHAVE_CONFIG_H",
    ],
    includes = ["."],
    linkstatic = 1,
    visibility = [
        "//visibility:public",
    ],
)
