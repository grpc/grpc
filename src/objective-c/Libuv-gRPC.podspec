# This file has been automatically generated from a template file.
# Please make modifications to
# `templates/src/objective-c/libuv-gRPC.podspec.template` instead. This
# file can be regenerated from the template by running
# `tools/buildgen/generate_projects.sh`.

# Libuv CocoaPods podspec

# Copyright 2021, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
#   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

Pod::Spec.new do |spec|

  pod_version           = "0.0.10"
  libuv_version         = "1.37.0"

  spec.name         = "Libuv-gRPC"
  spec.version      = pod_version
  spec.summary      = "gRPC-Only Libuv Pod"

  spec.description  = <<-DESC
    Libuv pod intended to be used only by gRPC iOS. libuv is a multi-platform
    support library with a focus on asynchronous I/O. It was primarily developed
    for use by Node.js, but it's also used by Luvit, Julia, pyuv, and others.
  DESC

  spec.homepage     = "https://libuv.org/"

  spec.license  = { :type => 'Mixed', :file => 'LICENSE' }
  spec.author    = "libuv"

  # When using multiple platforms
  spec.ios.deployment_target = '9.0'
  spec.osx.deployment_target = '10.10'
  spec.tvos.deployment_target = '10.0'
  spec.watchos.deployment_target = '4.0'

  spec.source       = { :git => "https://github.com/libuv/libuv.git", :tag => "v#{libuv_version}" }

  name = 'uv'
  spec.module_name = name
  spec.header_mappings_dir = 'include'
  spec.header_dir = name

  spec.subspec 'Interface' do |ss|
    ss.header_mappings_dir = 'include'
    ss.source_files = "include/uv.h",
                      "include/uv/errno.h",
                      "include/uv/threadpool.h",
                      "include/uv/version.h",
                      "include/uv/tree.h",
                      "include/uv/unix.h",
                      "include/uv/darwin.h"
  end

  spec.subspec 'Implementation' do |ss|
    ss.header_mappings_dir = 'src'
    ss.source_files =
    "src/fs-poll.c",
    "src/idna.c",
    "src/inet.c",
    "src/strscpy.c",
    "src/threadpool.c",
    "src/timer.c",
    "src/uv-data-getter-setters.c",
    "src/uv-common.c",
    "src/version.c",
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
    "src/unix/signal.c",
    "src/unix/stream.c",
    "src/unix/tcp.c",
    "src/unix/thread.c",
    "src/unix/tty.c",
    "src/unix/udp.c",
    "src/unix/bsd-ifaddrs.c",
    "src/unix/darwin.c",
    "src/unix/fsevents.c",
    "src/unix/kqueue.c",
    "src/unix/darwin-proctitle.c",
    "src/unix/proctitle.c",
    "src/heap-inl.h",
    "src/idna.h",
    "src/queue.h",
    "src/strscpy.h",
    "src/uv-common.h",
    "src/unix/atomic-ops.h",
    "src/unix/internal.h",
    "src/unix/spinlock.h"

    ss.dependency "#{spec.name}/Interface", pod_version
  end

  spec.requires_arc = false

  spec.pod_target_xcconfig = {
    'HEADER_SEARCH_PATHS' => '"$(inherited)" "$(PODS_TARGET_SRCROOT)/include"',
    'USER_HEADER_SEARCH_PATHS' => '"$(PODS_TARGET_SRCROOT)" "$(PODS_TARGET_SRCROOT)/src" "$(PODS_TARGET_SRCROOT)/include"',
    'CLANG_WARN_STRICT_PROTOTYPES' => 'NO',
    'CLANG_WARN_DOCUMENTATION_COMMENTS' => 'NO',
    'USE_HEADERMAP' => 'NO',
    'ALWAYS_SEARCH_USER_PATHS' => 'NO',
    'GCC_TREAT_WARNINGS_AS_ERRORS' => 'NO',
    'GCC_WARN_INHIBIT_ALL_WARNINGS' => 'YES'
  }

  spec.libraries = 'c++'

  spec.compiler_flags =
    "-D_LARGEFILE_SOURCE",
    "-D_FILE_OFFSET_BITS=64",
    "-D_GNU_SOURCE",
    "-D_DARWIN_USE_64_BIT_INODE=1",
    "-D_DARWIN_UNLIMITED_SELECT=1"

end
