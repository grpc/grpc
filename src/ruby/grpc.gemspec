# -*- ruby -*-
# encoding: utf-8
$LOAD_PATH.push File.expand_path('../lib', __FILE__)
require 'grpc/version'

Gem::Specification.new do |s|
  s.name          = 'grpc'
  s.version       = Google::RPC::VERSION
  s.authors       = ['gRPC Authors']
  s.email         = 'tbetbetbe@gmail.com'
  s.homepage      = 'https://github.com/google/grpc/tree/master/src/ruby'
  s.summary       = 'Google RPC system in Ruby'
  s.description   = 'Send RPCs from Ruby using Google\'s RPC system'

  s.files         = `git ls-files`.split("\n")
  s.test_files    = `git ls-files -- spec/*`.split("\n")
  s.executables   = `git ls-files -- bin/*.rb`.split("\n").map do |f|
    File.basename(f)
  end
  s.require_paths = ['lib']
  s.platform      = Gem::Platform::RUBY

  s.add_dependency 'faraday', '~> 0.9'
  s.add_dependency 'google-protobuf', '~> 3.0.0alpha.1.1'
  s.add_dependency 'logging', '~> 1.8'
  s.add_dependency 'jwt', '~> 1.2.1'
  s.add_dependency 'minitest', '~> 5.4'  # reqd for interop tests
  s.add_dependency 'multi_json', '1.10.1'
  s.add_dependency 'signet', '~> 0.6.0'
  s.add_dependency 'xray', '~> 1.1'

  s.add_development_dependency 'bundler', '~> 1.7'
  s.add_development_dependency 'rake', '~> 10.0'
  s.add_development_dependency 'rake-compiler', '~> 0'
  s.add_development_dependency 'rubocop', '~> 0.28.0'
  s.add_development_dependency 'rspec', '~> 3.0'

  s.extensions = %w(ext/grpc/extconf.rb)
end
