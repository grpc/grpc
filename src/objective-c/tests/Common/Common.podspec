Pod::Spec.new do |s|
    s.name     = 'Common'
    s.version  = "0.0.1"
    s.license  = "Apache License, Version 2.0"
    s.authors  = { 'gRPC contributors' => 'grpc-io@googlegroups.com' }
    s.homepage = "https://grpc.io/"
    s.summary = "Shared common test utils"
    s.source = { :git => 'https://github.com/grpc/grpc.git' }

    s.ios.deployment_target = '10.0'
    s.osx.deployment_target = '10.12'
    s.tvos.deployment_target = '12.0'
    s.watchos.deployment_target = '6.0'
    s.framework = 'XCTest'

    s.dependency "gRPC-ProtoRPC"

    s.source_files  = '**/*.{m}'
    s.public_header_files = '**/*.{h}'
  end
