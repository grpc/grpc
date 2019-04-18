#!/usr/bin/env ruby

# Copyright 2015 gRPC authors.
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

# Sample gRPC server that implements the EchoWithoutProtobuf service.
#
# Usage: $ path/to/echo_server.rb

this_dir = File.expand_path(File.dirname(__FILE__))
$LOAD_PATH.unshift(this_dir) unless $LOAD_PATH.include?(this_dir)

require 'grpc'
require 'echo_services_noprotobuf'

# EchoServer is simple server that implements the EchoWithoutProtobuf server.
class EchoServer < EchoWithoutProtobuf::Service
  # echo implements the EchoWithoutProtobuf 'Echo' rpc method.
  def echo(echo_req, _unused_call)
    echo_req
  end
end

# main starts an RpcServer that receives requests to EchoWithoutProtobuf at the sample
# server port.
def main
  s = GRPC::RpcServer.new
  s.add_http2_port('0.0.0.0:50051', :this_port_is_insecure)
  s.handle(EchoServer)
  s.run_till_terminated
end

main
