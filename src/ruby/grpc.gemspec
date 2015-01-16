# encoding: utf-8
$LOAD_PATH.push File.expand_path('../lib', __FILE__)
require 'grpc/version'

Gem::Specification.new do |s|
  s.name          = 'grpc'
  s.version       = Google::RPC::VERSION
  s.authors       = ['One Platform Team']
  s.email         = 'stubby-team@google.com'
  s.homepage      = 'http://go/grpc'
  s.summary       = 'Google RPC system in Ruby'
  s.description   = 'Send RPCs from Ruby'

  s.files         = `git ls-files`.split("\n")
  s.test_files    = `git ls-files -- spec/*`.split("\n")
  s.executables   = `git ls-files -- bin/*.rb`.split("\n").map do |f|
    File.basename(f)
  end
  s.require_paths = ['lib']
  s.platform      = Gem::Platform::RUBY

  s.add_dependency 'xray'
  s.add_dependency 'logging', '~> 1.8'
  s.add_dependency 'google-protobuf', '~> 3.0.0alpha'
  s.add_dependency 'minitest', '~> 5.4'  # reqd for interop tests

  s.add_development_dependency 'bundler', '~> 1.7'
  s.add_development_dependency 'rake', '~> 10.0'
  s.add_development_dependency 'rake-compiler', '~> 0'
  s.add_development_dependency 'rubocop', '~> 0.28.0'
  s.add_development_dependency 'rspec', '~> 3.0'

  s.extensions = %w(ext/grpc/extconf.rb)
end
