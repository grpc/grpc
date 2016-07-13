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

  name = 'GRPCCronet'

  s.module_name = name

  # When creating a dynamic framework, copy the headers under `include/grpc/` into the root of
  # the `Headers/` directory of the framework (i.e., not under `Headers/include/grpc`).
  s.header_mappings_dir = 'include/grpc'

  # The above has an undesired effect when creating a static library: It forces users to write
  # includes like `#include <gRPC-Cronet/grpc.h>`. `s.header_dir` adds a path prefix to that, and
  # because Cocoapods lets omit the pod name when including headers of static libraries, the
  # following lets users write `#include <grpc/grpc.h>`.
  s.header_dir = 'grpc'

  # To compile the library, we need the user headers search path (quoted includes) to point to the
  # root of the repo, and the system headers search path (angled includes) to point to `include/`.
  # Cocoapods effectively clones the repo under `<Podfile dir>/Pods/gRPC-Cronet/`, and sets a build
  # variable called `$(PODS_ROOT)` to `<Podfile dir>/Pods/`, so we use that.
  #
  # Relying on the file structure under $(PODS_ROOT) isn't officially supported in Cocoapods, as it
  # is taken as an implementation detail. We've asked for an alternative, and have been told that
  # what we're doing should keep working: https://github.com/CocoaPods/CocoaPods/issues/4386
  #
  # The `src_root` value of `$(PODS_ROOT)/gRPC-Cronet` assumes Cocoapods is installing this pod from
  # its remote repo. For local development of this library, enabled by using `:path` in the Podfile,
  # that assumption is wrong. In such case, the following settings need to be reset with the
  # appropriate value of `src_root`. This can be accomplished in the `pre_install` hook of the
  # Podfile; see `src/objective-c/tests/Podfile` for an example.
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
                      'test/core/end2end/cq_verifier.{c,h}',
                      'test/core/end2end/end2end_tests.{c,h}',
                      'test/core/end2end/tests/*.{c,h}',
                      'test/core/end2end/data/*.{c,h}',
                      'test/core/util/test_config.{c,h}',
                      'test/core/util/port.h',
                      'test/core/util/port_posix.c',
                      'test/core/util/port_server_client.{c,h}'

    ss.dependency 'gRPC-Core', version
    ss.dependency 'CronetFramework'
  end
end
