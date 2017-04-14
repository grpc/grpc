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

require 'etc'
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

windows = RUBY_PLATFORM =~ /mingw|mswin/

grpc_root = File.expand_path(File.join(File.dirname(__FILE__), '../../../..'))

grpc_config = ENV['GRPC_CONFIG'] || 'opt'

ENV['MACOSX_DEPLOYMENT_TARGET'] = '10.7'

ENV['AR'] = RbConfig::CONFIG['AR'] + ' rcs'
ENV['CC'] = RbConfig::CONFIG['CC']
ENV['LD'] = ENV['CC']

ENV['AR'] = 'libtool -o' if RUBY_PLATFORM =~ /darwin/

ENV['EMBED_OPENSSL'] = 'true'
ENV['EMBED_ZLIB'] = 'true'
ENV['EMBED_CARES'] = 'true'
ENV['ARCH_FLAGS'] = RbConfig::CONFIG['ARCH_FLAG']
ENV['ARCH_FLAGS'] = '-arch i386 -arch x86_64' if RUBY_PLATFORM =~ /darwin/
ENV['CFLAGS'] = '-DGPR_BACKWARDS_COMPATIBILITY_MODE'

output_dir = File.expand_path(RbConfig::CONFIG['topdir'])
grpc_lib_dir = File.join(output_dir, 'libs', grpc_config)
ENV['BUILDDIR'] = output_dir

unless windows
  puts 'Building internal gRPC into ' + grpc_lib_dir
  nproc = 4
  nproc = Etc.nprocessors * 2 if Etc.respond_to? :nprocessors
  system("make -j#{nproc} -C #{grpc_root} #{grpc_lib_dir}/libgrpc.a CONFIG=#{grpc_config}")
  exit 1 unless $? == 0
end

$CFLAGS << ' -I' + File.join(grpc_root, 'include')
$LDFLAGS << ' ' + File.join(grpc_lib_dir, 'libgrpc.a') unless windows
if grpc_config == 'gcov'
  $CFLAGS << ' -O0 -fprofile-arcs -ftest-coverage'
  $LDFLAGS << ' -fprofile-arcs -ftest-coverage -rdynamic'
end

if grpc_config == 'dbg'
  $CFLAGS << ' -O0'
end

$LDFLAGS << ' -Wl,-wrap,memcpy' if RUBY_PLATFORM =~ /linux/
$LDFLAGS << ' -static' if windows

$CFLAGS << ' -std=c99 '
$CFLAGS << ' -Wall '
$CFLAGS << ' -Wextra '
$CFLAGS << ' -pedantic '
$CFLAGS << ' -Wno-format '

output = File.join('grpc', 'grpc_c')
puts 'Generating Makefile for ' + output
create_makefile(output)

strip_tool = RbConfig::CONFIG['STRIP']
strip_tool = 'strip -x' if RUBY_PLATFORM =~ /darwin/

if grpc_config == 'opt'
  File.open('Makefile.new', 'w') do |o|
    o.puts 'hijack: all strip'
    o.puts
    File.foreach('Makefile') do |i|
      o.puts i
    end
    o.puts
    o.puts 'strip:'
    o.puts "\t$(ECHO) Stripping $(DLLIB)"
    o.puts "\t$(Q) #{strip_tool} $(DLLIB)"
  end
  File.rename('Makefile.new', 'Makefile')
end
