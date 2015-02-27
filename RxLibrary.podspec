Pod::Spec.new do |s|
  s.name     = 'RxLibrary'
  s.version  = '0.0.1'
  s.summary  = 'Reactive Extensions library for iOS'
  s.homepage = 'https://github.com/grpc/grpc/tree/master/src/objective-c/RxLibrary'
  s.license  = 'New BSD'
  s.authors  = { 'Jorge Canizales' => 'jcanizales@google.com' }

  # s.source = { :git => 'https://github.com/grpc/grpc.git', :tag => 'release-0_5_0' }
  s.source_files = 'src/objective-c/RxLibrary/*.{h,m}', 'src/objective-c/RxLibrary/transformations/*.{h,m}', 'src/objective-c/RxLibrary/private/*.{h,m}'
  s.private_header_files = 'src/objective-c/RxLibrary/private/*.h'

  s.platform = :ios
  s.ios.deployment_target = '6.0'
  s.requires_arc = true
end
