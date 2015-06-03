Pod::Spec.new do |s|
  s.name     = 'Route_guide'
  s.version  = '0.0.1'
  s.summary  = 'Protobuf library generated from route_guide.proto'
  s.license  = 'New BSD'

  s.ios.deployment_target = '6.0'
  s.osx.deployment_target = '10.8'

  s.subspec 'Messages' do |ms|
    ms.source_files = '*.pbobjc.{h,m}'
    ms.requires_arc = false
    ms.dependency 'Protobuf', '~> 3.0.0-alpha-3'
  end

  s.subspec 'Services' do |ss|
    ss.source_files = '*.pbrpc.{h,m}'
    ss.requires_arc = true
    ss.dependency 'gRPC', '~> 0.5'
    ss.dependency 'Route_guide/Messages'
  end
end
