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

def check_grpc_root
  grpc_root = ENV['GRPC_ROOT']
  if grpc_root.nil?
    r = File.expand_path(File.join(File.dirname(__FILE__), '../../../..'))
    grpc_root = r if File.exist?(File.join(r, 'include/grpc/grpc.h'))
  end
  grpc_root
end

grpc_pkg_config = system('pkg-config --exists grpc')

if grpc_pkg_config
  $CFLAGS << ' ' + `pkg-config --static --cflags grpc`.strip + ' '
  $LDFLAGS << ' ' + `pkg-config --static --libs grpc`.strip + ' '
else
  dir_config('grpc', HEADER_DIRS, LIB_DIRS)
  fail 'libdl not found' unless have_library('dl', 'dlopen')
  fail 'zlib not found' unless have_library('z', 'inflate')
  begin
    fail 'Fail' unless have_library('gpr', 'gpr_now')
    fail 'Fail' unless have_library('grpc', 'grpc_channel_destroy')
  rescue
    # Check to see if GRPC_ROOT is defined or available
    grpc_root = check_grpc_root

    # Stop if there is still no grpc_root
    exit 1 if grpc_root.nil?

    grpc_config = ENV['GRPC_CONFIG'] || 'opt'
    if ENV.key?('GRPC_LIB_DIR')
      grpc_lib_dir = File.join(grpc_root, ENV['GRPC_LIB_DIR'])
    else
      grpc_lib_dir = File.join(File.join(grpc_root, 'libs'), grpc_config)
    end
    unless File.exist?(File.join(grpc_lib_dir, 'libgrpc.a'))
      print "Building internal gRPC\n"
      system("make -C #{grpc_root} static_c CONFIG=#{grpc_config}")
    end
    $CFLAGS << ' -I' + File.join(grpc_root, 'include')
    $LDFLAGS << ' -L' + grpc_lib_dir
    raise 'gpr not found' unless have_library('gpr', 'gpr_now')
    raise 'grpc not found' unless have_library('grpc', 'grpc_channel_destroy')
  end
end

$CFLAGS << ' -std=c99 '
$CFLAGS << ' -Wall '
$CFLAGS << ' -Wextra '
$CFLAGS << ' -pedantic '
$CFLAGS << ' -Werror '

create_makefile('grpc/grpc')
