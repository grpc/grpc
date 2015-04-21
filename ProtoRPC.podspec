Pod::Spec.new do |s|
  s.name     = 'ProtoRPC'
  s.version  = '0.0.1'
  s.summary  = 'RPC library for ProtocolBuffers, based on gRPC'
  s.homepage = 'https://github.com/grpc/grpc/tree/master/src/objective-c/ProtoRPC'
  s.license  = 'New BSD'
  s.authors  = { 'Jorge Canizales' => 'jcanizales@google.com' }

  # s.source = { :git => 'https://github.com/grpc/grpc.git', :tag => 'release-0_5_0' }
  s.source_files = 'src/objective-c/ProtoRPC/*.{h,m}'

  s.platform = :ios
  s.ios.deployment_target = '6.0'
  s.requires_arc = true

  s.dependency 'gRPC', '~> 0.0'
  s.dependency 'RxLibrary', '~> 0.0'
end
