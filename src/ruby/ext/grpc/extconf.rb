# Copyright 2015-2016, Google Inc.
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
require 'net/http'
require 'grpc/version'
require 'pp'

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

build_from_source = ENV['GRPC_RUBY_BUILD_FROM_SOURCE']

# see https://www.ruby-lang.org/en/news/2013/12/21/ruby-version-policy-changes-with-2-1-0/
abi = "#{RbConfig::CONFIG['MAJOR']}.#{RbConfig::CONFIG['MINOR']}.0"
ruby_version = RbConfig::CONFIG['RUBY_PROGRAM_VERSION']
local_platform = Gem::Platform.local
ext = RbConfig::CONFIG['DLEXT']
target_os = RbConfig::CONFIG['target_os']
target_cpu = RbConfig::CONFIG['target_cpu']
# These describe the expected location of the file online
remote_path = "grpc-precompiled-binaries/ruby/grpc/v#{GRPC::VERSION}"
remote_name = "#{abi}-#{RbConfig::CONFIG['target_os']}-#{RbConfig::CONFIG['target_cpu']}.#{ext}"

if build_from_source
  download_success = false
else
  begin
    print "Attempting to download precompiled extension\n"
    print "From storage.googleapis.com/#{remote_path}/#{remote_name}\n"
    Net::HTTP.start('storage.googleapis.com') do |http|
      http.request_get("/#{remote_path}/#{remote_name}") do |resp|
        resp.value()
        open("grpc.#{ext}", 'wb') do |ext_bin_file|
          resp.read_body do |segment|
            ext_bin_file.write(segment)
          end
        end
      end
    end
    print "Downloaded precompiled extension\n"
    download_success = true
  rescue Net::HTTPExceptions => e
    download_success = false
  end
end

if download_success
  # Create dummy makefile
  open('Makefile', 'w') do |makefile|
    makefile.write("all:\n\ttrue\n\ninstall:\n\ttrue")
  end
  $makefile_created = true
else
  fail 'libdl not found' unless have_library('dl', 'dlopen')
  fail 'zlib not found' unless have_library('z', 'inflate')

  grpc_root = File.expand_path(File.join(File.dirname(__FILE__), '../../../..'))

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
  $LDFLAGS << ' ' + File.join(grpc_lib_dir, 'libgrpc.a')
  $LDFLAGS << ' ' + File.join(grpc_lib_dir, 'libgpr.a')
  if grpc_config == 'gcov'
    $CFLAGS << ' -O0 -fprofile-arcs -ftest-coverage'
    $LDFLAGS << ' -fprofile-arcs -ftest-coverage -rdynamic'
  end

  $CFLAGS << ' -std=c99 '
  $CFLAGS << ' -Wall '
  $CFLAGS << ' -Wextra '
  $CFLAGS << ' -pedantic '
  $CFLAGS << ' -Werror '

  $LDFLAGS << ' -lssl '
  $LDFLAGS << ' -lcrypto '

  create_makefile('grpc/grpc')
end
