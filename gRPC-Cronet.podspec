# GRPC CocoaPods podspec
# This file has been automatically generated from a template file.
# Please look at the templates directory instead.
# This file can be regenerated from the template by running
# tools/buildgen/generate_projects.sh

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


Pod::Spec.new do |s|
  s.name     = 'gRPC-Cronet'
  version = '0.14.0'
  s.version  = version
  s.summary  = 'Integration of CroNet framework into gRPC'
  s.homepage = 'http://www.grpc.io'
  s.license  = 'New BSD'
  s.authors  = { 'The gRPC contributors' => 'grpc-packages@google.com' }

  s.source = {
    :git => 'https://github.com/grpc/grpc.git',
    :tag => "release-#{version.gsub(/\./, '_')}-objectivec-#{version}",
  }

  s.ios.deployment_target = '7.1'
  s.osx.deployment_target = '10.9'
  s.requires_arc = false

  name = 'grpc'

  s.module_name = name

  s.header_mappings_dir = '.'

  src_root = '$(PODS_ROOT)/gRPC-Cronet'
  s.pod_target_xcconfig = {
    'GRPC_SRC_ROOT' => src_root,
    'HEADER_SEARCH_PATHS' => '"$(inherited)" "$(GRPC_SRC_ROOT)/include"',
    'USER_HEADER_SEARCH_PATHS' => '"$(GRPC_SRC_ROOT)"',
    # If we don't set these two settings, `include/grpc/support/time.h` and
    # `src/core/lib/support/string.h` shadow the system `<time.h>` and `<string.h>`, breaking the
    # build.
    'USE_HEADERMAP' => 'NO',
    'ALWAYS_SEARCH_USER_PATHS' => 'NO',
  }

  s.subspec 'Interface' do |ss|
    ss.header_mappings_dir = 'include/grpc'
    ss.source_files = 'include/grpc/grpc_cronet.h'
  end

  s.subspec 'Implementation' do |ss|
    ss.header_mappings_dir = '.'

    ss.source_files = 'src/core/ext/transport/cronet/client/secure/cronet_channel_create.c',
                      'src/core/ext/transport/cronet/transport/cronet_transport.c',
                      'test/core/end2end/**/*.{c,h}',
                      'test/core/util'

    ss.dependency 'gRPC-Core', version
  end
end
