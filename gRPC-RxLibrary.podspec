# This file has been automatically generated from a template file.
# Please make modifications to
# `templates/gRPC-RxLibrary.podspec.template` instead. This file can be
# regenerated from the template by running
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
  s.name     = 'gRPC-RxLibrary'
  version = '1.56.0-pre2'
  s.version  = version
  s.summary  = 'Reactive Extensions library for iOS/OSX.'
  s.homepage = 'https://grpc.io'
  s.license  = 'Apache License, Version 2.0'
  s.authors  = { 'The gRPC contributors' => 'grpc-packages@google.com' }

  s.source = {
    :git => 'https://github.com/grpc/grpc.git',
    :tag => "v#{version}",
  }

  s.ios.deployment_target = '10.0'
  s.osx.deployment_target = '10.12'
  s.tvos.deployment_target = '12.0'
  s.watchos.deployment_target = '6.0'

  name = 'RxLibrary'
  s.module_name = name
  s.header_dir = name

  s.default_subspec = 'Interface', 'Implementation'

  src_dir = 'src/objective-c/RxLibrary'
  s.subspec 'Interface' do |ss|
    ss.header_mappings_dir = "#{src_dir}"
    ss.source_files = "#{src_dir}/*.h"
    ss.public_header_files = "#{src_dir}/*.h"
  end

  s.subspec 'Implementation' do |ss|
    ss.header_mappings_dir = "#{src_dir}"
    ss.source_files = "#{src_dir}/*.m", "#{src_dir}/**/*.{h,m}"
    ss.private_header_files = "#{src_dir}/**/*.h"

    ss.dependency "#{s.name}/Interface"
  end

  src_dir = 'src/objective-c/RxLibrary'
  s.source_files = "#{src_dir}/*.{h,m}", "#{src_dir}/**/*.{h,m}"
  s.private_header_files = "#{src_dir}/private/*.h"
  s.header_mappings_dir = "#{src_dir}"

  s.pod_target_xcconfig = {
    'CLANG_WARN_STRICT_PROTOTYPES' => 'NO',
    'CLANG_CXX_LANGUAGE_STANDARD' => 'c++14',
  }
end
