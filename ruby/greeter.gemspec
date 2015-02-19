# -*- ruby -*-
# encoding: utf-8

Gem::Specification.new do |s|
  s.name          = 'grpc'
  s.version       = '0.0.0'
  s.authors       = ['gRPC Authors']
  s.email         = 'temiola@google.com'
  s.homepage      = 'https://github.com/google/grpc-common'
  s.summary       = 'gRPC Ruby overview sample'
  s.description   = 'Demonstrates how'

  s.files         = `git ls-files -- ruby/*`.split("\n")
  s.executables   = `git ls-files -- ruby/greeter*.rb`.split("\n").map do |f|
    File.basename(f)
  end
  s.require_paths = ['lib']
  s.platform      = Gem::Platform::RUBY

  s.add_dependency 'grpc', '~> 0.0.1'

  s.add_development_dependency 'bundler', '~> 1.7'
end
