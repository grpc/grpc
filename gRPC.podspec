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
  version = '1.56.1'
  s.version  = version
  s.summary  = 'gRPC client library for iOS/OSX'
  s.homepage = 'https://grpc.io'
  s.license  = 'Apache License, Version 2.0'
  s.authors  = { 'The gRPC contributors' => 'grpc-packages@google.com' }

  s.source = {
    :git => 'https://github.com/grpc/grpc.git',
    :tag => "v#{version}",
  }

  name = 'GRPCClient'
  s.module_name = name
  s.header_dir = name

  s.default_subspec = 'Interface', 'GRPCCore', 'Interface-Legacy'

  s.pod_target_xcconfig = {
    # This is needed by all pods that depend on gRPC-RxLibrary:
    'CLANG_ALLOW_NON_MODULAR_INCLUDES_IN_FRAMEWORK_MODULES' => 'YES',
    'CLANG_WARN_STRICT_PROTOTYPES' => 'NO',
    'CLANG_CXX_LANGUAGE_STANDARD' => 'c++14',
  }

  s.ios.deployment_target = '10.0'
  s.osx.deployment_target = '10.12'
  s.tvos.deployment_target = '12.0'
  s.watchos.deployment_target = '6.0'

  s.subspec 'Interface-Legacy' do |ss|
    ss.header_mappings_dir = 'src/objective-c/GRPCClient'

    ss.public_header_files = "src/objective-c/GRPCClient/GRPCCall+ChannelArg.h",
                             "src/objective-c/GRPCClient/GRPCCall+ChannelCredentials.h",
                             "src/objective-c/GRPCClient/GRPCCall+Cronet.h",
                             "src/objective-c/GRPCClient/GRPCCall+OAuth2.h",
                             "src/objective-c/GRPCClient/GRPCCall+Tests.h",
                             "src/objective-c/GRPCClient/GRPCCallLegacy.h",
                             "src/objective-c/GRPCClient/GRPCTypes.h"

    ss.source_files = "src/objective-c/GRPCClient/GRPCCall+ChannelArg.h",
                      "src/objective-c/GRPCClient/GRPCCall+ChannelCredentials.h",
                      "src/objective-c/GRPCClient/GRPCCall+Cronet.h",
                      "src/objective-c/GRPCClient/GRPCCall+OAuth2.h",
                      "src/objective-c/GRPCClient/GRPCCall+Tests.h",
                      "src/objective-c/GRPCClient/GRPCCallLegacy.h",
                      "src/objective-c/GRPCClient/GRPCTypes.h",
                      "src/objective-c/GRPCClient/GRPCTypes.m"
    ss.dependency "gRPC-RxLibrary/Interface", version

    s.ios.deployment_target = '10.0'
    s.osx.deployment_target = '10.12'
    s.tvos.deployment_target = '12.0'
    s.watchos.deployment_target = '6.0'
  end

  s.subspec 'Interface' do |ss|
    ss.header_mappings_dir = 'src/objective-c/GRPCClient'

    ss.public_header_files = 'src/objective-c/GRPCClient/GRPCCall.h',
                             'src/objective-c/GRPCClient/GRPCCall+Interceptor.h',
                             'src/objective-c/GRPCClient/GRPCCallOptions.h',
                             'src/objective-c/GRPCClient/GRPCInterceptor.h',
                             'src/objective-c/GRPCClient/GRPCTransport.h',
                             'src/objective-c/GRPCClient/GRPCDispatchable.h',
                             'src/objective-c/GRPCClient/version.h'

    ss.source_files = 'src/objective-c/GRPCClient/GRPCCall.h',
                      'src/objective-c/GRPCClient/GRPCCall.m',
                      'src/objective-c/GRPCClient/GRPCCall+Interceptor.h',
                      'src/objective-c/GRPCClient/GRPCCall+Interceptor.m',
                      'src/objective-c/GRPCClient/GRPCCallOptions.h',
                      'src/objective-c/GRPCClient/GRPCCallOptions.m',
                      'src/objective-c/GRPCClient/GRPCDispatchable.h',
                      'src/objective-c/GRPCClient/GRPCInterceptor.h',
                      'src/objective-c/GRPCClient/GRPCInterceptor.m',
                      'src/objective-c/GRPCClient/GRPCTransport.h',
                      'src/objective-c/GRPCClient/GRPCTransport.m',
                      'src/objective-c/GRPCClient/internal/*.h',
                      'src/objective-c/GRPCClient/private/GRPCTransport+Private.h',
                      'src/objective-c/GRPCClient/private/GRPCTransport+Private.m',
                      'src/objective-c/GRPCClient/version.h'

    ss.dependency "#{s.name}/Interface-Legacy", version

    s.ios.deployment_target = '10.0'
    s.osx.deployment_target = '10.12'
    s.tvos.deployment_target = '12.0'
    s.watchos.deployment_target = '6.0'
  end

  s.subspec 'GRPCCore' do |ss|
    ss.header_mappings_dir = 'src/objective-c/GRPCClient'

    ss.public_header_files = 'src/objective-c/GRPCClient/GRPCCall+ChannelCredentials.h',
                             'src/objective-c/GRPCClient/GRPCCall+Cronet.h',
                             'src/objective-c/GRPCClient/GRPCCall+OAuth2.h',
                             'src/objective-c/GRPCClient/GRPCCall+Tests.h',
                             'src/objective-c/GRPCClient/GRPCCall+ChannelArg.h'
    ss.private_header_files = 'src/objective-c/GRPCClient/private/GRPCCore/*.h'
    ss.source_files = 'src/objective-c/GRPCClient/private/GRPCCore/*.{h,m}',
                      'src/objective-c/GRPCClient/GRPCCall+ChannelArg.h',
                      'src/objective-c/GRPCClient/GRPCCall+ChannelArg.m',
                      'src/objective-c/GRPCClient/GRPCCall+ChannelCredentials.h',
                      'src/objective-c/GRPCClient/GRPCCall+ChannelCredentials.m',
                      'src/objective-c/GRPCClient/GRPCCall+Cronet.h',
                      'src/objective-c/GRPCClient/GRPCCall+Cronet.m',
                      'src/objective-c/GRPCClient/GRPCCall+OAuth2.h',
                      'src/objective-c/GRPCClient/GRPCCall+OAuth2.m',
                      'src/objective-c/GRPCClient/GRPCCall+Tests.h',
                      'src/objective-c/GRPCClient/GRPCCall+Tests.m',
                      'src/objective-c/GRPCClient/GRPCCallLegacy.m'

    # Certificates, to be able to establish TLS connections:
    ss.resource_bundles = { 'gRPCCertificates' => ['etc/roots.pem'] }

    ss.dependency "#{s.name}/Interface-Legacy", version
    ss.dependency "#{s.name}/Interface", version
    ss.dependency 'gRPC-Core', version
    ss.dependency 'gRPC-RxLibrary', version

    s.ios.deployment_target = '10.0'
    s.osx.deployment_target = '10.12'
    s.tvos.deployment_target = '12.0'
    s.watchos.deployment_target = '6.0'
  end

  s.subspec 'GRPCCoreCronet' do |ss|
    ss.header_mappings_dir = 'src/objective-c/GRPCClient'

    ss.source_files = 'src/objective-c/GRPCClient/GRPCCall+Cronet.h',
                      'src/objective-c/GRPCClient/GRPCCall+Cronet.m',
                      'src/objective-c/GRPCClient/private/GRPCCore/GRPCCoreCronet/*.{h,m}'
    ss.dependency "#{s.name}/GRPCCore", version
    ss.dependency 'gRPC-Core/Cronet-Implementation', version
    ss.dependency 'CronetFramework'

    ss.ios.deployment_target = '10.0'
  end

  # CFStream is now default. Leaving this subspec only for compatibility purpose.
  s.subspec 'CFStream' do |ss|
    ss.dependency "#{s.name}/GRPCCore", version

    s.ios.deployment_target = '10.0'
    s.osx.deployment_target = '10.12'
    s.tvos.deployment_target = '12.0'
    s.watchos.deployment_target = '6.0'
  end

  s.subspec 'InternalTesting' do |ss|
    ss.dependency "#{s.name}/GRPCCore", version
    ss.public_header_files = 'src/objective-c/GRPCClient/internal_testing/*.h'
    ss.source_files = 'src/objective-c/GRPCClient/internal_testing/*.{h,m}'
    ss.header_mappings_dir = 'src/objective-c/GRPCClient'

    s.ios.deployment_target = '10.0'
    s.osx.deployment_target = '10.12'
    s.tvos.deployment_target = '12.0'
    s.watchos.deployment_target = '6.0'
  end
end
