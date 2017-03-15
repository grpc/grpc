#!/usr/bin/env ruby

# Copyright 2016, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

# smoke test for a grpc-using app that receives and
# handles process-ending signals

this_dir = File.expand_path(File.dirname(__FILE__))
protos_lib_dir = File.join(this_dir, 'lib')
grpc_lib_dir = File.join(File.dirname(this_dir), 'lib')
$LOAD_PATH.unshift(grpc_lib_dir) unless $LOAD_PATH.include?(grpc_lib_dir)
$LOAD_PATH.unshift(protos_lib_dir) unless $LOAD_PATH.include?(protos_lib_dir)
$LOAD_PATH.unshift(this_dir) unless $LOAD_PATH.include?(this_dir)

require 'grpc'
require 'echo_server'
require 'client_control_services_pb'

def main
  this_dir = File.expand_path(File.dirname(__FILE__))
  lib_dir = File.join(File.dirname(this_dir), 'lib')

  server_port = '50051'
  STDERR.puts "start server"
  server_runner = ServerRunner.new(server_port)
  server_runner.run

  sleep 1

  client_control_port = '50052'

  STDERR.puts "start client"
  client_path = File.join(this_dir, "sig_handling_client.rb")
  client_pid = Process.spawn(RbConfig.ruby, client_path, "--client_control_port=#{client_control_port}")
  control_stub = ClientControl::ClientController::Stub.new("localhost:#{client_control_port}", :this_channel_is_insecure)

  sleep 1

  control_stub.create_client_stub(ClientControl::CreateClientStubRequest.new(server_address: "localhost:#{server_port}"))

  count = 0
  while count < 5
    control_stub.do_echo_rpc(ClientControl::DoEchoRpcRequest.new(request: 'hello'))
    Process.kill('SIGTERM', client_pid)
    Process.kill('SIGINT', client_pid)
    count += 1
  end

  control_stub.shutdown(ClientControl::Void.new)
  Process.wait(client_pid)

  client_exit_code = $?.exitstatus

  if client_exit_code != 0
    raise "term sig test failure: client exit code: #{client_exit_code}"
  end

  server_runner.stop
end

main
