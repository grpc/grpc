# Copyright 2021 gRPC authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Any modifications to this file must be accompanied with corresponding changes
# to `generate_libuv_source_wrappers.py`.

load("@bazel_skylib//lib:selects.bzl", "selects")

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

config_setting(
    name = "freebsd",
    constraint_values = ["@platforms//os:freebsd"],
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

# The following architectures are found in
# https://github.com/bazelbuild/bazel/blob/master/src/main/java/com/google/devtools/build/lib/rules/apple/ApplePlatform.java
config_setting(
    name = "tvos_x86_64",
    values = {"cpu": "tvos_x86_64"},
)

config_setting(
    name = "tvos_arm64",
    values = {"cpu": "tvos_arm64"},
)

config_setting(
    name = "watchos_i386",
    values = {"cpu": "watchos_i386"},
)

config_setting(
    name = "watchos_x86_64",
    values = {"cpu": "watchos_x86_64"},
)

config_setting(
    name = "watchos_armv7k",
    values = {"cpu": "watchos_armv7k"},
)

config_setting(
    name = "watchos_arm64_32",
    values = {"cpu": "watchos_arm64_32"},
)

selects.config_setting_group(
    name = "apple",
    match_any = [
        ":darwin",
        ":darwin_x86_64",
        ":darwin_arm64",
        ":darwin_arm64e",
        "ios_x86_64",
        "ios_armv7",
        "ios_armv7s",
        "ios_arm64",
        "tvos_x86_64",
        "tvos_arm64",
        "watchos_i386",
        "watchos_x86_64",
        "watchos_armv7k",
        "watchos_arm64_32",
    ],
)

COMMON_LIBUV_HEADERS = [
    "include/uv.h",
    "include/uv/errno.h",
    "include/uv/threadpool.h",
    "include/uv/version.h",
    "include/uv/tree.h",
    "src/heap-inl.h",
    "src/idna.h",
    "src/queue.h",
    "src/strscpy.h",
    "src/uv-common.h",
]

UNIX_LIBUV_HEADERS = [
    "include/uv/unix.h",
    "src/unix/atomic-ops.h",
    "src/unix/internal.h",
    "src/unix/spinlock.h",
]

LINUX_LIBUV_HEADERS = [
    "include/uv/linux.h",
    "src/unix/linux-syscalls.h",
]

ANDROID_LIBUV_HEADERS = []

DARWIN_LIBUV_HEADERS = [
    "include/uv/darwin.h",
    "src/unix/darwin-stub.h",
]

WINDOWS_LIBUV_HEADERS = [
    "include/uv/win.h",
    "src/win/atomicops-inl.h",
    "src/win/fs-fd-hash-inl.h",
    "src/win/handle-inl.h",
    "src/win/internal.h",
    "src/win/req-inl.h",
    "src/win/stream-inl.h",
    "src/win/winapi.h",
    "src/win/winsock.h",
]

BSD_LIBUV_HEADERS = [
    "include/uv/bsd.h",
]

COMMON_LIBUV_SOURCES = [
    "src/fs-poll.c",
    "src/idna.c",
    "src/inet.c",
    "src/random.c",
    "src/strscpy.c",
    "src/threadpool.c",
    "src/timer.c",
    "src/uv-data-getter-setters.c",
    "src/uv-common.c",
    "src/version.c",
]

UNIX_LIBUV_SOURCES = [
    "src/unix/async.c",
    "src/unix/core.c",
    "src/unix/dl.c",
    "src/unix/fs.c",
    "src/unix/getaddrinfo.c",
    "src/unix/getnameinfo.c",
    "src/unix/loop.c",
    "src/unix/loop-watcher.c",
    "src/unix/pipe.c",
    "src/unix/poll.c",
    "src/unix/process.c",
    "src/unix/random-devurandom.c",
    "src/unix/signal.c",
    "src/unix/stream.c",
    "src/unix/tcp.c",
    "src/unix/thread.c",
    "src/unix/tty.c",
    "src/unix/udp.c",
]

LINUX_LIBUV_SOURCES = [
    "src/unix/linux-core.c",
    "src/unix/linux-inotify.c",
    "src/unix/linux-syscalls.c",
    "src/unix/procfs-exepath.c",
    "src/unix/random-getrandom.c",  # freebsd, android, linux
    "src/unix/random-sysctl-linux.c",
    # "src/unix/sysinfo-loadavg.c",  # TODO(hork): only needed on Cygwin and MSYS now
    "src/unix/epoll.c",  # linux, android
]

ANDROID_LIBUV_SOURCES = [
    "src/unix/android-ifaddrs.c",
    "src/unix/pthread-fixes.c",
]

DARWIN_LIBUV_SOURCES = [
    "src/unix/darwin.c",
    "src/unix/fsevents.c",
    "src/unix/kqueue.c",
    "src/unix/darwin-proctitle.c",
    "src/unix/random-getentropy.c",  # darwin. TODO(hork): ALSO NEEDED FOR OpenBSD
]

BSD_LIBUV_SOURCES = [
    "src/unix/bsd-proctitle.c",
]

FREEBSD_LIBUV_SOURCES = [
    "src/unix/freebsd.c",
]

WINDOWS_LIBUV_SOURCES = [
    "src/win/async.c",
    "src/win/core.c",
    "src/win/detect-wakeup.c",
    "src/win/dl.c",
    "src/win/error.c",
    "src/win/fs-event.c",
    "src/win/fs.c",
    "src/win/getaddrinfo.c",
    "src/win/getnameinfo.c",
    "src/win/handle.c",
    "src/win/loop-watcher.c",
    "src/win/pipe.c",
    "src/win/poll.c",
    "src/win/process-stdio.c",
    "src/win/process.c",
    "src/win/signal.c",
    "src/win/stream.c",
    "src/win/tcp.c",
    "src/win/thread.c",
    "src/win/tty.c",
    "src/win/udp.c",
    "src/win/util.c",
    "src/win/winapi.c",
    "src/win/winsock.c",
]

###############################################################################
## SPECIAL CASES
#
# Used in both Darwin and Unix builds.
#
# This BUILD file is source of truth for platform configurations. This file
# needs to be handled separately for our build. See
# tools/buildgen/generate_libuv_source_wrappers.py
UNIX_PROCTITLE = ["src/unix/proctitle.c"]

# Used in both Darwin and BSD builds. Requires special templating
BSD_IFADDRS = ["src/unix/bsd-ifaddrs.c"]
###############################################################################

GCC_COPTS = [
    "-D_LARGEFILE_SOURCE",
    "-D_FILE_OFFSET_BITS=64",
    "-D_GNU_SOURCE",
    "-pthread",
    "--std=gnu89",
    "-pedantic",
    "-Wno-error",
    "-Wno-strict-aliasing",
    "-Wstrict-aliasing",
    "-O2",
    "-Wno-implicit-function-declaration",
    "-Wno-unused-function",
    "-Wno-unused-variable",
]

DARWIN_COPTS = [
    "-D_DARWIN_USE_64_BIT_INODE=1",
    "-D_DARWIN_UNLIMITED_SELECT=1",
]

cc_library(
    name = "libuv",
    srcs = COMMON_LIBUV_SOURCES + select({
        ":android": UNIX_LIBUV_SOURCES + LINUX_LIBUV_SOURCES + ANDROID_LIBUV_SOURCES + UNIX_PROCTITLE,
        ":apple": UNIX_LIBUV_SOURCES + DARWIN_LIBUV_SOURCES + UNIX_PROCTITLE + BSD_IFADDRS,
    	  ":freebsd": UNIX_LIBUV_SOURCES + BSD_LIBUV_SOURCES + FREEBSD_LIBUV_SOURCES + BSD_IFADDRS,
        ":windows": WINDOWS_LIBUV_SOURCES,
        "//conditions:default": UNIX_LIBUV_SOURCES + LINUX_LIBUV_SOURCES + UNIX_PROCTITLE,
    }),
    hdrs = COMMON_LIBUV_HEADERS + select({
        ":android": UNIX_LIBUV_HEADERS + LINUX_LIBUV_HEADERS + ANDROID_LIBUV_HEADERS,
        ":apple": UNIX_LIBUV_HEADERS + DARWIN_LIBUV_HEADERS,
        ":freebsd": UNIX_LIBUV_HEADERS + BSD_LIBUV_HEADERS,
        ":windows": WINDOWS_LIBUV_HEADERS,
        "//conditions:default": UNIX_LIBUV_HEADERS + LINUX_LIBUV_HEADERS,
    }),
    copts = [
    ] + select({
        ":apple": DARWIN_COPTS + GCC_COPTS,
        ":windows": [
            "-DWIN32_LEAN_AND_MEAN",
            "-D_WIN32_WINNT=0x0600",
        ],
        "//conditions:default": GCC_COPTS,
    }),
    includes = [
        "include",
        "src",
    ],
    linkopts = select({
        ":apple": [],
        ":windows": [
            # "-Xcrosstool-compilation-mode=$(COMPILATION_MODE)",
            "-DEFAULTLIB:Iphlpapi.lib",
            "-DEFAULTLIB:Psapi.lib",
            "-DEFAULTLIB:User32.lib",
            "-DEFAULTLIB:Userenv.lib",
        ],
        "//conditions:default": [
            "-ldl",
        ],
    }),
    visibility = [
        "//visibility:public",
    ],
)
