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

def check_to_status(error)
  my_status = error.to_status
  fail('GRPC BadStatus#to_status not expected to return nil') if my_status.nil?
  fail('GRPC BadStatus#to_status code expected to be 2') unless my_status.code == 2
  fail('GRPC BadStatus#to_status details expected to be unknown') unless my_status.details == 'unknown'
  fail('GRPC BadStatus#to_status metadata expected to be empty hash') unless my_status.metadata == {}
  fail('GRPC library loaded after BadStatus#to_status') if GRPC::Core.const_defined?(:Channel)
end

def check_to_rpc_status(error)
  my_rpc_status = error.to_rpc_status
  fail('GRPC BadStatus#to_rpc_status expected to return nil') unless my_rpc_status.nil?
  fail('GRPC library loaded after BadStatus#to_rpc_status') if GRPC::Core.const_defined?(:Channel)
end

def main
  fail('GRPC constant loaded before expected') if Object.const_defined?(:GRPC)
  require 'grpc/errors'
  fail('GRPC constant not loaded when expected') unless Object.const_defined?(:GRPC)
  fail('GRPC BadStatus not loaded after required') unless GRPC.const_defined?(:BadStatus)
  fail('GRPC Core not loaded after required') unless GRPC.const_defined?(:Core)
  fail('GRPC StatusCodes not loaded after required') unless GRPC::Core.const_defined?(:StatusCodes)
  fail('GRPC library loaded before required') if GRPC::Core.const_defined?(:Channel)
  error = GRPC::BadStatus.new 2, 'unknown'
  check_to_status(error)
  check_to_rpc_status(error)
  require 'grpc'
  fail('GRPC library not loaded after required') unless GRPC::Core.const_defined?(:Channel)
end

main
