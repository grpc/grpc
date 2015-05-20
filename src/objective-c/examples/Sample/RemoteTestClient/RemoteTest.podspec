Pod::Spec.new do |s|
  s.name     = 'RemoteTest'
  s.version  = '0.0.1'
  s.summary  = 'Protobuf library generated from test.proto, messages.proto, and empty.proto'
  s.homepage = 'https://github.com/grpc/grpc/tree/master/src/objective-c/examples/Sample/RemoteTestClient'
  s.license  = 'New BSD'
  s.authors  = { 'Jorge Canizales' => 'jcanizales@google.com' }

  s.ios.deployment_target = '6.0'
  s.osx.deployment_target = '10.8'

  s.subspec 'Messages' do |ms|
    ms.source_files = '*.pbobjc.{h,m}'
    ms.requires_arc = false
    ms.dependency 'Protobuf', '~> 3.0'
  end

  s.subspec 'Services' do |ss|
    ss.source_files = '*.pbrpc.{h,m}'
    ss.requires_arc = true
    ss.dependency 'gRPC', '~> 0.0'
    ss.dependency 'RemoteTest/Messages'
  end
end
