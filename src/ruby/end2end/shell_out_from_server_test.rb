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

require 'sanity_check_dlopen'
require 'grpc'
require 'end2end_common'

class ShellOutEchoServer < Echo::EchoServer::Service
  def echo(echo_req, _)
    # Attempt to repro https://github.com/grpc/grpc/issues/38210
    Kernel.system('echo "test"')
    Echo::EchoReply.new(response: echo_req.request)
  end
end

def main
  server_runner = ServerRunner.new(ShellOutEchoServer)
  server_port = server_runner.run
  stub = Echo::EchoServer::Stub.new("localhost:#{server_port}", :this_channel_is_insecure)
  10.times do
    stub.echo(Echo::EchoRequest.new(request: 'hello'), deadline: Time.now + 300)
  end
  server_runner.stop
end

main
