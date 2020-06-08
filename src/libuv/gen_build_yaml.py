#!/usr/bin/env python2.7

# Copyright 2020 gRPC authors.
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

import os
import sys
import yaml
import glob

out = {}

common_uv_srcs = glob.glob("third_party/libuv/src/*.c")

unix_uv_srcs = [
    "third_party/libuv/src/unix/async.c",
    "third_party/libuv/src/unix/core.c",
    "third_party/libuv/src/unix/dl.c",
    "third_party/libuv/src/unix/fs.c",
    "third_party/libuv/src/unix/getaddrinfo.c",
    "third_party/libuv/src/unix/getnameinfo.c",
    "third_party/libuv/src/unix/loop.c",
    "third_party/libuv/src/unix/loop-watcher.c",
    "third_party/libuv/src/unix/pipe.c",
    "third_party/libuv/src/unix/poll.c",
    "third_party/libuv/src/unix/process.c",
    "third_party/libuv/src/unix/signal.c",
    "third_party/libuv/src/unix/stream.c",
    "third_party/libuv/src/unix/tcp.c",
    "third_party/libuv/src/unix/thread.c",
    "third_party/libuv/src/unix/tty.c",
    "third_party/libuv/src/unix/udp.c",
    "third_party/libuv/src/unix/random-devurandom.c",
    "third_party/libuv/src/unix/random-getrandom.c",
]

linux_uv_srcs = [
    "third_party/libuv/src/unix/linux-core.c",
    "third_party/libuv/src/unix/linux-inotify.c",
    "third_party/libuv/src/unix/linux-syscalls.c",
    "third_party/libuv/src/unix/procfs-exepath.c",
    "third_party/libuv/src/unix/proctitle.c",
    "third_party/libuv/src/unix/random-sysctl-linux.c",
    "third_party/libuv/src/unix/sysinfo-loadavg.c",
]

darwin_uv_srcs = [
    "third_party/libuv/src/unix/bsd-ifaddrs.c",
    "third_party/libuv/src/unix/darwin.c",
    "third_party/libuv/src/unix/darwin-proctitle.c",
    "third_party/libuv/src/unix/fsevents.c",
    "third_party/libuv/src/unix/kqueue.c",
    "third_party/libuv/src/unix/proctitle.c",
    "third_party/libuv/src/unix/random-getentropy.c",
]

win_uv_srcs = glob.glob("third_party/libuv/src/win/*.c")

common_uv_hdrs = [
    "third_party/libuv/include/uv.h",
    "third_party/libuv/include/uv/errno.h",
    "third_party/libuv/include/uv/threadpool.h",
    "third_party/libuv/include/uv/version.h",
    "third_party/libuv/include/uv/tree.h",
] + glob.glob("third_party/libuv/src/*.h")

unix_uv_hdrs = [
    "third_party/libuv/include/uv/unix.h",
    "third_party/libuv/src/unix/atomic-ops.h",
    "third_party/libuv/src/unix/internal.h",
    "third_party/libuv/src/unix/spinlock.h",
]

linux_uv_hdrs = [
    "third_party/libuv/include/uv/linux.h",
    "third_party/libuv/src/unix/linux-syscalls.h",
]

darwin_uv_hdrs = [
    "third_party/libuv/include/uv/darwin.h",
]

win_uv_hdrs = [
    "third_party/libuv/include/uv/win.h",
] + glob.glob("third_party/libuv/src/win/*.h")

try:
    out['libs'] = [{
        'name': 'uv_linux',
        'defaults': 'uv_linux',
        'build': 'private',
        'language': 'c',
        'secure': False,
        'src': sorted(common_uv_srcs + unix_uv_srcs + linux_uv_srcs),
        'headers': sorted(common_uv_hdrs + unix_uv_hdrs + linux_uv_hdrs),
    }, {
        'name': 'uv_darwin',
        'defaults': 'uv_darwin',
        'build': 'private',
        'language': 'c',
        'secure': False,
        'src': sorted(common_uv_srcs + unix_uv_srcs + darwin_uv_srcs),
        'headers': sorted(common_uv_hdrs + unix_uv_hdrs + darwin_uv_hdrs),
    }, {
        'name': 'uv_win',
        'defaults': 'uv_win',
        'build': 'private',
        'language': 'c',
        'secure': False,
        'src': sorted(common_uv_srcs + win_uv_srcs),
        'headers': sorted(common_uv_hdrs + win_uv_hdrs),
    }]
except:
    pass

print(yaml.dump(out))
