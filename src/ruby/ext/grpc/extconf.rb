# Copyright 2015, Google Inc.
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
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

require 'mkmf'

LIBDIR = RbConfig::CONFIG['libdir']
INCLUDEDIR = RbConfig::CONFIG['includedir']

if ENV.key? 'GRPC_ROOT'
  GRPC_ROOT = ENV['GRPC_ROOT']
  if ENV.key? 'GRPC_LIB_DIR'
    GRPC_LIB_DIR = ENV['GRPC_LIB_DIR']
  else
    GRPC_LIB_DIR = 'libs/opt'
  end
else
  GRPC_ROOT = nil
end

HEADER_DIRS = [
  # Search /opt/local (Mac source install)
  '/opt/local/include',

  # Search /usr/local (Source install)
  '/usr/local/include',

  # Check the ruby install locations
  INCLUDEDIR
]

LIB_DIRS = [
  # Search /opt/local (Mac source install)
  '/opt/local/lib',

  # Search /usr/local (Source install)
  '/usr/local/lib',

  # Check the ruby install locations
  LIBDIR
]

unless GRPC_ROOT.nil?
  HEADER_DIRS.unshift File.join(GRPC_ROOT, 'include')
  LIB_DIRS.unshift File.join(GRPC_ROOT, GRPC_LIB_DIR)
end

def crash(msg)
  print(" extconf failure: #{msg}\n")
  exit 1
end

dir_config('grpc', HEADER_DIRS, LIB_DIRS)

$CFLAGS << ' -std=c89 '
$CFLAGS << ' -Wno-implicit-function-declaration '
$CFLAGS << ' -Wno-pointer-sign '
$CFLAGS << ' -Wno-return-type '
$CFLAGS << ' -Wall '
$CFLAGS << ' -pedantic '

$LDFLAGS << ' -lgrpc -lgpr -ldl'

crash('need grpc lib') unless have_library('grpc', 'grpc_channel_destroy')
have_library('grpc', 'grpc_channel_destroy')
crash('need gpr lib') unless have_library('gpr', 'gpr_now')
create_makefile('grpc/grpc')
