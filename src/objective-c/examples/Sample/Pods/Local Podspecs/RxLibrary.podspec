Pod::Spec.new do |s|
  s.name         = 'RxLibrary'
  s.version      = '0.0.1'
  s.summary      = 'Reactive Extensions library for iOS'
  s.author = {
    'Jorge Canizales' => 'jcanizales@google.com'
  }
  s.source_files = '*.{h,m}'
  s.platform = :ios
  s.ios.deployment_target = '6.0'
  s.requires_arc = true
end
