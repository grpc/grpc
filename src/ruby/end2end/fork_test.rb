#!/usr/bin/env ruby
#
# Copyright 2016 gRPC authors.
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
protos_lib_dir = File.join(this_dir, 'lib')
grpc_lib_dir = File.join(File.dirname(this_dir), 'lib')
$LOAD_PATH.unshift(grpc_lib_dir) unless $LOAD_PATH.include?(grpc_lib_dir)
$LOAD_PATH.unshift(protos_lib_dir) unless $LOAD_PATH.include?(protos_lib_dir)
$LOAD_PATH.unshift(this_dir) unless $LOAD_PATH.include?(this_dir)

require 'grpc'
require 'end2end_common'

def main
  server_runner = ServerRunner.new(EchoServerImpl)
  server_port = server_runner.run
  stub = Echo::EchoServer::Stub.new("localhost:#{server_port}", :this_channel_is_insecure)
  stub.echo(Echo::EchoRequest.new(request: 'hello'), deadline: Time.now + 300)
  STDERR.puts "GRPC::pre_fork begin"
  GRPC::prefork
  STDERR.puts "GRPC::pre_fork done"
  pid = fork do
    STDERR.puts "child: GRPC::postfork_child begin"
    GRPC::postfork_child
    #STDERR.puts "child: GRPC::postfork_child done"
    #stub.echo(Echo::EchoRequest.new(request: 'hello'), deadline: Time.now + 300)
    #STDERR.puts "child: first post-fork RPC done"
    #stub.echo(Echo::EchoRequest.new(request: 'hello'), deadline: Time.now + 300)
    #STDERR.puts "child: done"
  end
  STDERR.puts "parent: GRPC::postfork_parent begin"
  GRPC::postfork_parent
  STDERR.puts "parent: GRPC::postfork_parent done"
  stub.echo(Echo::EchoRequest.new(request: 'hello'), deadline: Time.now + 300)
  STDERR.puts "parent: first post-fork RPC done"
  stub.echo(Echo::EchoRequest.new(request: 'hello'), deadline: Time.now + 300)
  STDERR.puts "parent: second post-fork RPC done"
  Process.wait pid
  STDERR.puts "parent: done"
end

main
