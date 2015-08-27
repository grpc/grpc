# -*- ruby -*-
# encoding: utf-8
$LOAD_PATH.push File.expand_path('../lib', __FILE__)
require 'grpc/version'

Gem::Specification.new do |s|
  s.name          = 'grpc'
  s.version       = GRPC::VERSION
  s.authors       = ['gRPC Authors']
  s.email         = 'temiola@google.com'
  s.homepage      = 'https://github.com/google/grpc/tree/master/src/ruby'
  s.summary       = 'GRPC system in Ruby'
  s.description   = 'Send RPCs from Ruby using GRPC'
  s.license       = 'BSD-3-Clause'

  s.required_ruby_version = '>= 2.0.0'
  s.requirements << 'libgrpc ~> 0.10.0 needs to be installed'

  s.files         = `git ls-files`.split("\n")
  s.test_files    = `git ls-files -- spec/*`.split("\n")
  s.executables   = `git ls-files -- bin/*.rb`.split("\n").map do |f|
    File.basename(f)
  end
  s.require_paths = ['lib']
  s.platform      = Gem::Platform::RUBY

  s.add_dependency 'google-protobuf', '~> 3.0.0alpha.1.1'
  s.add_dependency 'googleauth', '~> 0.4'  # reqd for interop tests
  s.add_dependency 'logging', '~> 2.0'
  s.add_dependency 'minitest', '~> 5.4'  # reqd for interop tests

  s.add_development_dependency 'simplecov', '~> 0.9'
  s.add_development_dependency 'bundler', '~> 1.9'
  s.add_development_dependency 'rake', '~> 10.4'
  s.add_development_dependency 'rake-compiler', '~> 0.9'
  s.add_development_dependency 'rspec', '~> 3.2'
  s.add_development_dependency 'rubocop', '~> 0.30.0'

  s.extensions = %w(ext/grpc/extconf.rb)
end
