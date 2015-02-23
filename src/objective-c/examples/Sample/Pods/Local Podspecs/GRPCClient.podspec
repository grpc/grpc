Pod::Spec.new do |s|
  s.name         = 'GRPCClient'
  s.version      = '0.0.1'
  s.summary      = 'Generic gRPC client library for iOS'
  s.author = {
    'Jorge Canizales' => 'jcanizales@google.com'
  }
  s.source_files = '*.{h,m}', 'private/*.{h,m}'
  s.private_header_files = 'private/*.h'
  s.platform = :ios
  s.ios.deployment_target = '6.0'
  s.requires_arc = true
  s.dependency 'RxLibrary', '~> 0.0'
end
