# -*- ruby -*-
# encoding: utf-8

Gem::Specification.new do |s|
  s.name          = 'grpc-demo'
  s.version       = '1.0.0'
  s.authors       = ['gRPC Authors']
  s.email         = 'temiola@google.com'
  s.homepage      = 'https://github.com/grpc/grpc'
  s.summary       = 'gRPC Ruby overview sample'
  s.description   = 'Simple demo of using gRPC from Ruby'

  s.files         = `git ls-files -- ruby/*`.split("\n")
  s.executables   = `git ls-files -- ruby/greeter*.rb ruby/route_guide/*.rb`.split("\n").map do |f|
    File.basename(f)
  end
  s.require_paths = ['lib']
  s.platform      = Gem::Platform::RUBY

  s.add_dependency 'grpc', '~> 1.0'
  s.add_dependency 'multi_json', '~> 1.13.1'
  s.add_development_dependency 'bundler', '~> 1.7'
end
