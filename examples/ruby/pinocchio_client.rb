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

# Sample app that connects to a Greeter service.
#
# Usage: $ path/to/greeter_client.rb

this_dir = File.expand_path(File.dirname(__FILE__))
lib_dir = File.join(this_dir, 'lib')
$LOAD_PATH.unshift(lib_dir) unless $LOAD_PATH.include?(lib_dir)

require 'grpc'
require 'helloworld_services_pb'
require 'pinocchio_services_pb'

# require "pry"
require "pry-byebug"

def run_echo_stream_request(pinocchio_stub)
  reqs = []
  (0..10).each { |i|
    reqs.push(Instacart::Infra::Pinocchio::V2::EchoRequest.new(message: "stream request: #{i}"))
  }
  resp = pinocchio_stub.echo_stream_request(reqs)
  puts resp.message
end

def run_echo_stream_response(pinocchio_stub)
  req = Instacart::Infra::Pinocchio::V2::EchoRequest.new(message: "test stream response")
  resp = pinocchio_stub.echo_stream_response(req)
  resp.each { |r| puts r.message }
end

def run_echo_stream_request_response(pinocchio_stub)
  reqs = []
  (0..10).each { |i|
    reqs.push(Instacart::Infra::Pinocchio::V2::EchoRequest.new(message: "stream request: #{i}"))
  }
  resp = pinocchio_stub.echo_stream_request_response(reqs)
  resp.each { |r| puts r.message }
end

def main
  begin
    pinocchio_host = "localhost:50051"
    pinocchio_stub = Instacart::Infra::Pinocchio::V2::PinocchioService::Stub.new(pinocchio_host, :this_channel_is_insecure)
    message = pinocchio_stub.echo(Instacart::Infra::Pinocchio::V2::EchoRequest.new(message: "Hello, Pinocchio!"))
    puts "PinocchioService received: #{message.message}"

    run_echo_stream_request(pinocchio_stub)
    run_echo_stream_response(pinocchio_stub)
    run_echo_stream_request_response(pinocchio_stub)

  rescue GRPC::BadStatus => e
    abort "ERROR: #{e.message}"
  end
end

main
