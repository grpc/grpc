Pod::Spec.new do |s|
  s.name     = 'gRPC'
  s.version  = '0.5.1'
  s.summary  = 'gRPC client library for iOS/OSX'
  s.homepage = 'http://www.grpc.io'
  s.license  = 'New BSD'
  s.authors  = { 'The gRPC contributors' => 'grpc-packages@google.com' }

  # s.source = { :git => 'https://github.com/grpc/grpc.git',
  #              :tag => 'release-0_9_1-objectivec-0.5.1' }

  s.ios.deployment_target = '6.0'
  s.osx.deployment_target = '10.8'
  s.requires_arc = true

  s.subspec 'RxLibrary' do |rs|
    rs.summary  = 'Reactive Extensions library for iOS.'

    rs.source_files = 'src/objective-c/RxLibrary/*.{h,m}',
                      'src/objective-c/RxLibrary/transformations/*.{h,m}',
                      'src/objective-c/RxLibrary/private/*.{h,m}'
    rs.private_header_files = 'src/objective-c/RxLibrary/private/*.h'
  end

  s.subspec 'C-Core' do |cs|
    cs.summary  = 'Core cross-platform gRPC library, written in C.'

    cs.source_files = 'src/core/**/*.{h,c}', 'include/grpc/*.h', 'include/grpc/**/*.h'
    cs.private_header_files = 'src/core/**/*.h'
    cs.header_mappings_dir = '.'
    cs.xcconfig = { 'HEADER_SEARCH_PATHS' => '"$(PODS_ROOT)/Headers/Build/gRPC" '
                                             '"$(PODS_ROOT)/Headers/Build/gRPC/include"' }
    cs.compiler_flags = '-GCC_WARN_INHIBIT_ALL_WARNINGS', '-w'

    cs.requires_arc = false
    cs.libraries = 'z'
    cs.dependency 'OpenSSL', '~> 1.0.200'
  end

  # This is a workaround for Cocoapods Issue #1437.
  # It renames time.h and string.h to grpc_time.h and grpc_string.h.
  # It needs to be here (top-level) instead of in the C-Core subspec because Cocoapods doesn't run
  # prepare_command's of subspecs.
  s.prepare_command = <<-CMD
    DIR_TIME="grpc/support"
    BAD_TIME="$DIR_TIME/time.h"
    GOOD_TIME="$DIR_TIME/grpc_time.h"
    if [ -f "include/$BAD_TIME" ];
    then
      grep -rl "$BAD_TIME" include/grpc src/core | xargs sed -i '' -e s@$BAD_TIME@$GOOD_TIME@g
      mv "include/$BAD_TIME" "include/$GOOD_TIME"
    fi

    DIR_STRING="src/core/support"
    BAD_STRING="$DIR_STRING/string.h"
    GOOD_STRING="$DIR_STRING/grpc_string.h"
    if [ -f "$BAD_STRING" ];
    then
      grep -rl "$BAD_STRING" include/grpc src/core | xargs sed -i '' -e s@$BAD_STRING@$GOOD_STRING@g
      mv "$BAD_STRING" "$GOOD_STRING"
    fi
  CMD

  s.subspec 'GRPCClient' do |gs|
    gs.summary = 'Objective-C wrapper around the core gRPC library.'

    gs.source_files = 'src/objective-c/GRPCClient/*.{h,m}',
                      'src/objective-c/GRPCClient/private/*.{h,m}'
    gs.private_header_files = 'src/objective-c/GRPCClient/private/*.h'

    gs.dependency 'gRPC/C-Core'
    # Is this needed in all dependents?
    gs.xcconfig = { 'HEADER_SEARCH_PATHS' => '"$(PODS_ROOT)/Headers/Public/gRPC/include"' }
    gs.dependency 'gRPC/RxLibrary'

    # Certificates, to be able to establish TLS connections:
    gs.resource_bundles = { 'gRPC' => ['etc/roots.pem'] }
  end

  s.subspec 'ProtoRPC' do |ps|
    ps.summary  = 'RPC library for ProtocolBuffers, based on gRPC'

    ps.source_files = 'src/objective-c/ProtoRPC/*.{h,m}'

    ps.dependency 'gRPC/GRPCClient'
    ps.dependency 'gRPC/RxLibrary'
    ps.dependency 'Protobuf', '~> 3.0'
  end
end
