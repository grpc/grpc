# -*- ruby -*-
# encoding: utf-8

Gem::Specification.new do |s|
  s.name          = 'distribtest'
  s.version       = '0.0.1'
  s.authors       = ['gRPC Authors']
  s.email         = 'jtattermusch@google.com'
  s.homepage      = 'https://github.com/grpc/grpc'
  s.summary       = 'gRPC Distribution test'

  s.files         = ['distribtest.rb']
  s.executables   = ['distribtest.rb']
  s.platform      = Gem::Platform::RUBY

  s.add_dependency 'grpc', '>=0'
  s.add_dependency 'public_suffix', '< 3.0'
  s.add_dependency 'jwt', '< 2.0'

  s.add_development_dependency 'bundler', '>= 1.9'
end
