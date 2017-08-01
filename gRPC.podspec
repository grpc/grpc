# This file has been automatically generated from a template file.
# Please make modifications to `templates/gRPC.podspec.template`
# instead. This file can be regenerated from the template by running
# `tools/buildgen/generate_projects.sh`.

# Copyright 2015 gRPC authors.
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


Pod::Spec.new do |s|
  s.name     = 'gRPC'
  version = '1.5.0-dev'
  s.version  = version
  s.summary  = 'gRPC client library for iOS/OSX'
  s.homepage = 'https://grpc.io'
  s.license  = 'Apache License, Version 2.0'
  s.authors  = { 'The gRPC contributors' => 'grpc-packages@google.com' }

  s.source = {
    :git => 'https://github.com/grpc/grpc.git',
    :tag => "v#{version}",
  }

  s.ios.deployment_target = '7.0'
  s.osx.deployment_target = '10.9'

  name = 'GRPCClient'
  s.module_name = name
  s.header_dir = name

  src_dir = 'src/objective-c/GRPCClient'
  s.source_files = "#{src_dir}/*.{h,m}", "#{src_dir}/**/*.{h,m}"
  s.private_header_files = "#{src_dir}/private/*.h"
  s.header_mappings_dir = "#{src_dir}"

  s.dependency 'gRPC-Core', version
  s.dependency 'gRPC-RxLibrary', version

  # Certificates, to be able to establish TLS connections:
  s.resource_bundles = { 'gRPCCertificates' => ['etc/roots.pem'] }

  s.pod_target_xcconfig = {
    # This is needed by all pods that depend on gRPC-RxLibrary:
    'CLANG_ALLOW_NON_MODULAR_INCLUDES_IN_FRAMEWORK_MODULES' => 'YES',
  }
end
