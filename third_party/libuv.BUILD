COMMON_LIBUV_HEADERS = [
    "include/uv.h",
    "include/uv/errno.h",
    "include/uv/threadpool.h",
    "include/uv/version.h",
    "include/uv/tree.h",
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

ANDROID_LIBUV_HEADERS = [
    "include/uv/android-ifaddrs.h",
]

DARWIN_LIBUV_HEADERS = [
    "include/uv/darwin.h",
]

WINDOWS_LIBUV_HEADERS = [
    "include/uv/win.h",
    "src/win/atomicops-inl.h",
    "src/win/handle-inl.h",
    "src/win/internal.h",
    "src/win/req-inl.h",
    "src/win/stream-inl.h",
    "src/win/winapi.h",
    "src/win/winsock.h",
]

COMMON_LIBUV_SOURCES = [
    "src/fs-poll.c",
    "src/heap-inl.h",
    "src/idna.c",
    "src/idna.h",
    "src/inet.c",
    "src/queue.h",
    "src/strscpy.c",
    "src/strscpy.h",
    "src/threadpool.c",
    "src/timer.c",
    "src/uv-data-getter-setters.c",
    "src/uv-common.c",
    "src/uv-common.h",
    "src/version.c",
]

UNIX_LIBUV_SOURCES = [
    "src/unix/async.c",
    "src/unix/atomic-ops.h",
    "src/unix/core.c",
    "src/unix/dl.c",
    "src/unix/fs.c",
    "src/unix/getaddrinfo.c",
    "src/unix/getnameinfo.c",
    "src/unix/internal.h",
    "src/unix/loop.c",
    "src/unix/loop-watcher.c",
    "src/unix/pipe.c",
    "src/unix/poll.c",
    "src/unix/process.c",
    "src/unix/signal.c",
    "src/unix/spinlock.h",
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
    "src/unix/linux-syscalls.h",
    "src/unix/procfs-exepath.c",
    "src/unix/proctitle.c",
    "src/unix/sysinfo-loadavg.c",
    "src/unix/sysinfo-memory.c",
]

ANDROID_LIBUV_SOURCES = [
    "src/unix/android-ifaddrs.c",
    "src/unix/pthread-fixes.c",
]

DARWIN_LIBUV_SOURCES = [
    "src/unix/bsd-ifaddrs.c",
    "src/unix/darwin.c",
    "src/unix/fsevents.c",
    "src/unix/kqueue.c",
    "src/unix/darwin-proctitle.c",
    "src/unix/proctitle.c",
]

WINDOWS_LIBUV_SOURCES = [
    "src/win/async.c",
    "src/win/atomicops-inl.h",
    "src/win/core.c",
    "src/win/detect-wakeup.c",
    "src/win/dl.c",
    "src/win/error.c",
    "src/win/fs-event.c",
    "src/win/fs.c",
    "src/win/getaddrinfo.c",
    "src/win/getnameinfo.c",
    "src/win/handle.c",
    "src/win/handle-inl.h",
    "src/win/internal.h",
    "src/win/loop-watcher.c",
    "src/win/pipe.c",
    "src/win/poll.c",
    "src/win/process-stdio.c",
    "src/win/process.c",
    "src/win/req-inl.h",
    "src/win/signal.c",
    "src/win/stream.c",
    "src/win/stream-inl.h",
    "src/win/tcp.c",
    "src/win/thread.c",
    "src/win/tty.c",
    "src/win/udp.c",
    "src/win/util.c",
    "src/win/winapi.c",
    "src/win/winapi.h",
    "src/win/winsock.c",
    "src/win/winsock.h",
]

cc_library(
    name = "libuv",
    srcs = select({
        "//tools/cc_target_os:android": COMMON_LIBUV_SOURCES + UNIX_LIBUV_SOURCES + LINUX_LIBUV_SOURCES + ANDROID_LIBUV_SOURCES,
        "//tools/cc_target_os:apple": COMMON_LIBUV_SOURCES + UNIX_LIBUV_SOURCES + DARWIN_LIBUV_SOURCES,
        "//tools/cc_target_os:windows": COMMON_LIBUV_SOURCES + WINDOWS_LIBUV_SOURCES,
        "//conditions:default": COMMON_LIBUV_SOURCES + UNIX_LIBUV_SOURCES + LINUX_LIBUV_SOURCES,
    }),
    hdrs = [
        "include/uv.h",
    ] + select({
        "//tools/cc_target_os:android": COMMON_LIBUV_HEADERS + UNIX_LIBUV_HEADERS + LINUX_LIBUV_HEADERS + ANDROID_LIBUV_HEADERS,
        "//tools/cc_target_os:apple": COMMON_LIBUV_HEADERS + UNIX_LIBUV_HEADERS + DARWIN_LIBUV_HEADERS,
        "//tools/cc_target_os:windows": COMMON_LIBUV_HEADERS + WINDOWS_LIBUV_HEADERS,
        "//conditions:default": COMMON_LIBUV_HEADERS + UNIX_LIBUV_HEADERS + LINUX_LIBUV_HEADERS,
    }),
    copts = [
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
    ] + select({
        "//tools/cc_target_os:apple": [],
        "//tools/cc_target_os:windows": [
            "-DWIN32_LEAN_AND_MEAN",
            "-D_WIN32_WINNT=0x0600",
        ],
        "//conditions:default": [
            "-Wno-tree-vrp",
            "-Wno-omit-frame-pointer",
            "-D_DARWIN_USE_64_BIT_INODE=1",
            "-D_DARWIN_UNLIMITED_SELECT=1",
        ],
    }),
    includes = [
        "include",
        "src",
    ],
    linkopts = select({
        "//tools/cc_target_os:windows": [
            "-Xcrosstool-compilation-mode=$(COMPILATION_MODE)",
            "-Wl,Iphlpapi.lib",
            "-Wl,Psapi.lib",
            "-Wl,User32.lib",
            "-Wl,Userenv.lib",
        ],
        "//conditions:default": [],
    }),
    visibility = [
        "//visibility:public",
    ],
)
