# -*- ruby -*-
# encoding: utf-8
require_relative 'version.rb'
require_relative 'platform.rb'
Gem::Specification.new do |s|
  s.name = 'grpc-native-debug'
  s.version = GRPC::NativeDebug::VERSION
  s.authors = ['grpc Authors']
  s.email = 'grpc-io@googlegroups.com'
  s.homepage = 'https://github.com/google/grpc/tree/master/src/ruby/nativedebug'
  s.summary = 'Debug symbols for the native library in Ruby gRPC'
  s.description = 'Debug symbols to compliment the native libraries in pre-compiled Ruby gRPC binary gems'
  s.license = 'Apache-2.0'

  s.files = %w( platform.rb version.rb README.md )
  s.files += Dir.glob('symbols/*.dbg')

  s.platform = GRPC::NativeDebug::PLATFORM
end
