Pod::Spec.new do |s|
  s.name     = 'Route_guide'
  s.version  = '0.0.1'
  s.summary  = 'Protobuf library generated from route_guide.proto'
  s.homepage = 'https://github.com/grpc/grpc/tree/master/src/objective-c/examples/Sample/RouteGuideClient'
  s.license  = 'New BSD'
  s.authors  = { 'Jorge Canizales' => 'jcanizales@google.com' }

  s.source_files = '*.pb.{h,m}'
  s.public_header_files = '*.pb.h'

  s.platform = :ios
  s.ios.deployment_target = '6.0'
  s.requires_arc = true

  s.dependency 'ProtocolBuffers', '~> 1.9'
end
