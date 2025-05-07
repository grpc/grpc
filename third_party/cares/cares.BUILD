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
    values = {"crosstool_top": "//external:android/crosstool"},
    # TODO: Use constraint_values to detect android after Bazel 7.0 platforms migration is finished
    # constraint_values = [ "@platforms//os:android" ],
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
        "src/lib/inet_ntop.c",
"src/lib/event/ares_event_configchg.c",
"src/lib/event/ares_event_poll.c",
"src/lib/event/ares_event_epoll.c",
"src/lib/event/ares_event_wake_pipe.c",
"src/lib/event/ares_event_select.c",
"src/lib/event/ares_event_kqueue.c",
"src/lib/event/ares_event_win32.c",
"src/lib/event/ares_event_thread.c",
"src/lib/ares_gethostbyaddr.c",
"src/lib/ares_set_socket_functions.c",
"src/lib/ares_options.c",
"src/lib/ares_getaddrinfo.c",
"src/lib/ares_init.c",
"src/lib/ares_android.c",
"src/lib/ares_destroy.c",
"src/lib/ares_getenv.c",
"src/lib/ares_addrinfo_localhost.c",
"src/lib/ares_send.c",
"src/lib/ares_free_hostent.c",
"src/lib/ares_data.c",
"src/lib/ares_parse_into_addrinfo.c",
"src/lib/ares_cancel.c",
"src/lib/ares_search.c",
"src/lib/ares_sysconfig_win.c",
"src/lib/ares_gethostbyname.c",
"src/lib/ares_addrinfo2hostent.c",
"src/lib/ares_freeaddrinfo.c",
"src/lib/ares_sysconfig.c",
"src/lib/ares_hosts_file.c",
"src/lib/ares_sortaddrinfo.c",
"src/lib/ares_free_string.c",
"src/lib/ares_qcache.c",
"src/lib/str/ares_str.c",
"src/lib/str/ares_strsplit.c",
"src/lib/str/ares_buf.c",
"src/lib/windows_port.c",
"src/lib/ares_socket.c",
"src/lib/ares_timeout.c",
"src/lib/ares_cookie.c",
"src/lib/dsa/ares_htable_vpstr.c",
"src/lib/dsa/ares_htable_strvp.c",
"src/lib/dsa/ares_htable_dict.c",
"src/lib/dsa/ares_htable_szvp.c",
"src/lib/dsa/ares_htable_vpvp.c",
"src/lib/dsa/ares_llist.c",
"src/lib/dsa/ares_htable.c",
"src/lib/dsa/ares_htable_asvp.c",
"src/lib/dsa/ares_array.c",
"src/lib/dsa/ares_slist.c",
"src/lib/ares_sysconfig_mac.c",
"src/lib/ares_process.c",
"src/lib/ares_close_sockets.c",
"src/lib/legacy/ares_create_query.c",
"src/lib/legacy/ares_parse_txt_reply.c",
"src/lib/legacy/ares_parse_ptr_reply.c",
"src/lib/legacy/ares_fds.c",
"src/lib/legacy/ares_expand_string.c",
"src/lib/legacy/ares_parse_uri_reply.c",
"src/lib/legacy/ares_parse_naptr_reply.c",
"src/lib/legacy/ares_parse_aaaa_reply.c",
"src/lib/legacy/ares_parse_srv_reply.c",
"src/lib/legacy/ares_parse_soa_reply.c",
"src/lib/legacy/ares_parse_mx_reply.c",
"src/lib/legacy/ares_parse_caa_reply.c",
"src/lib/legacy/ares_expand_name.c",
"src/lib/legacy/ares_getsock.c",
"src/lib/legacy/ares_parse_ns_reply.c",
"src/lib/legacy/ares_parse_a_reply.c",
"src/lib/ares_strerror.c",
"src/lib/ares_getnameinfo.c",
"src/lib/ares_version.c",
"src/lib/ares_library_init.c",
"src/lib/ares_sysconfig_files.c",
"src/lib/ares_metrics.c",
"src/lib/ares_conn.c",
"src/lib/ares_query.c",
"src/lib/inet_net_pton.c",
"src/lib/util/ares_rand.c",
"src/lib/util/ares_iface_ips.c",
"src/lib/util/ares_timeval.c",
"src/lib/util/ares_uri.c",
"src/lib/util/ares_threads.c",
"src/lib/util/ares_math.c",
"src/lib/ares_update_servers.c",
"src/lib/record/ares_dns_write.c",
"src/lib/record/ares_dns_record.c",
"src/lib/record/ares_dns_multistring.c",
"src/lib/record/ares_dns_mapping.c",
"src/lib/record/ares_dns_name.c",
"src/lib/record/ares_dns_parse.c",
    ],
    hdrs = [
        "ares_build.h",
        "ares_config.h",
        "include/ares_nameser.h",
"include/ares_dns_record.h",
"include/ares.h",
"include/ares_version.h",
"include/ares_dns.h",
        "src/lib/event/ares_event_win32.h",
"src/lib/event/ares_event.h",
"src/lib/include/ares_htable_asvp.h",
"src/lib/include/ares_llist.h",
"src/lib/include/ares_htable_vpstr.h",
"src/lib/include/ares_htable_strvp.h",
"src/lib/include/ares_htable_vpvp.h",
"src/lib/include/ares_htable_szvp.h",
"src/lib/include/ares_mem.h",
"src/lib/include/ares_htable_dict.h",
"src/lib/include/ares_array.h",
"src/lib/include/ares_buf.h",
"src/lib/include/ares_str.h",
"src/lib/ares_android.h",
"src/lib/ares_private.h",
"src/lib/ares_socket.h",
"src/lib/ares_ipv6.h",
"src/lib/config-dos.h",
"src/lib/ares_inet_net_pton.h",
"src/lib/str/ares_strsplit.h",
"src/lib/dsa/ares_slist.h",
"src/lib/dsa/ares_htable.h",
"src/lib/ares_data.h",
"src/lib/ares_getenv.h",
"src/lib/config-win32.h",
"src/lib/ares_conn.h",
"src/lib/ares_setup.h",
"src/lib/thirdparty/apple/dnsinfo.h",
"src/lib/util/ares_threads.h",
"src/lib/util/ares_time.h",
"src/lib/util/ares_iface_ips.h",
"src/lib/util/ares_math.h",
"src/lib/util/ares_rand.h",
"src/lib/util/ares_uri.h",
"src/lib/record/ares_dns_multistring.h",
"src/lib/record/ares_dns_private.h",
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
