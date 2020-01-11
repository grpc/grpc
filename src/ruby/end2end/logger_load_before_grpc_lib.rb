#!/usr/bin/env ruby

# Copyright 2018 gRPC authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

this_dir = File.expand_path(File.dirname(__FILE__))
grpc_lib_dir = File.join(File.dirname(this_dir), 'lib')
$LOAD_PATH.unshift(grpc_lib_dir) unless $LOAD_PATH.include?(grpc_lib_dir)

def main
  fail('GRPC constant loaded before expected') if Object.const_defined?(:GRPC)
  require 'grpc/logconfig'
  fail('GRPC constant not loaded when expected') unless Object.const_defined?(:GRPC)
  fail('GRPC DefaultLogger not loaded after required') unless GRPC.const_defined?(:DefaultLogger)
  fail('GRPC logger not included after required') unless GRPC.methods.include?(:logger)
  fail('GRPC Core loaded before required') if GRPC.const_defined?(:Core)
  require 'grpc'
  fail('GRPC Core not loaded after required') unless GRPC.const_defined?(:Core)
  fail('GRPC library not loaded after required') unless GRPC::Core.const_defined?(:Channel)
end

main
