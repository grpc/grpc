# -*- ruby -*-
# encoding: utf-8
require_relative 'version.rb'
Gem::Specification.new do |s|
  s.name = 'grpc-tools'
  s.version = GRPC::Tools::VERSION
  s.authors = ['grpc Authors']
  s.email = 'grpc-io@googlegroups.com'
  s.homepage = 'https://github.com/grpc/grpc/tree/master/src/ruby/tools'
  s.summary = 'Development tools for Ruby gRPC'
  s.description = 'protoc and the Ruby gRPC protoc plugin'
  s.license = 'Apache-2.0'

  s.files = %w( version.rb platform_check.rb README.md )
  s.files += Dir.glob('bin/**/*')

  s.bindir = 'bin'

  s.platform = Gem::Platform::RUBY

  s.executables = %w( grpc_tools_ruby_protoc grpc_tools_ruby_protoc_plugin )
end
