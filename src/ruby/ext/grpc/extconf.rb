# Copyright 2014, Google Inc.
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

HEADER_DIRS = [
    # First search the local development dir
    ENV['HOME'] + '/grpc_dev/include',

    # Then search /opt/local (Mac)
    '/opt/local/include',

    # Then search /usr/local (Source install)
    '/usr/local/include',

    # Check the ruby install locations
    INCLUDEDIR,

    # Finally fall back to /usr
    '/usr/include'
]

LIB_DIRS = [
    # First search the local development dir
    ENV['HOME'] + '/grpc_dev/lib',

    # Then search /opt/local for (Mac)
    '/opt/local/lib',

    # Then search /usr/local (Source install)
    '/usr/local/lib',

    # Check the ruby install locations
    LIBDIR,

    # Finally fall back to /usr
    '/usr/lib'
]

def crash(msg)
  print(" extconf failure: %s\n" % msg)
  exit 1
end

dir_config('grpc', HEADER_DIRS, LIB_DIRS)

$CFLAGS << ' -std=c89 '
$CFLAGS << ' -Wno-implicit-function-declaration '
$CFLAGS << ' -Wno-pointer-sign '
$CFLAGS << ' -Wno-return-type '
$CFLAGS << ' -Wall '
$CFLAGS << ' -pedantic '

$LDFLAGS << ' -lgrpc -lgpr -levent -levent_pthreads -levent_core '
$LDFLAGS << ' -lssl -lcrypto '
$DLDFLAGS << ' -Wl,-rpath,/usr/local/ssl/lib '

# crash('need grpc lib') unless have_library('grpc', 'grpc_channel_destroy')
#
# TODO(temiola): figure out why this stopped working, but the so is built OK
# and the tests pass

have_library('grpc', 'grpc_channel_destroy')
crash('need gpr lib') unless have_library('gpr', 'gpr_now')
create_makefile('grpc/grpc')
