#!/usr/bin/env ruby

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

# smoke test for a grpc-using app that receives and
# handles process-ending signals

require_relative './end2end_common'

# A service that calls back it's received_rpc_callback
# upon receiving an RPC. Used for synchronization/waiting
# for child process to start.
class ClientStartedService < Echo::EchoServer::Service
  def initialize(received_rpc_callback)
    @received_rpc_callback = received_rpc_callback
  end

  def echo(echo_req, _)
    @received_rpc_callback.call unless @received_rpc_callback.nil?
    @received_rpc_callback = nil
    Echo::EchoReply.new(response: echo_req.request)
  end
end

def main
  STDERR.puts 'start server'
  client_started = false
  client_started_mu = Mutex.new
  client_started_cv = ConditionVariable.new
  received_rpc_callback = proc do
    client_started_mu.synchronize do
      client_started = true
      client_started_cv.signal
    end
  end

  client_started_service = ClientStartedService.new(received_rpc_callback)
  server_runner = ServerRunner.new(client_started_service)
  server_port = server_runner.run
  STDERR.puts 'start client'
  control_stub, client_pid = start_client('sig_handling_client.rb', server_port)

  client_started_mu.synchronize do
    client_started_cv.wait(client_started_mu) until client_started
  end

  count = 0
  while count < 5
    control_stub.do_echo_rpc(
      ClientControl::DoEchoRpcRequest.new(request: 'hello'))
    Process.kill('SIGTERM', client_pid)
    Process.kill('SIGINT', client_pid)
    count += 1
  end

  cleanup(control_stub, client_pid, server_runner)
end

main
