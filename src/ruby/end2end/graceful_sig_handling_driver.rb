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

def main
  STDERR.puts 'start server'
  echo_service = EchoServerImpl.new
  server_runner = ServerRunner.new(echo_service)
  server_port = server_runner.run
  STDERR.puts 'start client'
  control_stub, client_pid = start_client('graceful_sig_handling_client.rb', server_port)
  # use receipt of one RPC to indicate that the child process is
  # ready
  echo_service.wait_for_first_rpc_received(20)
  # now get the client to send an RPC
  control_stub.do_echo_rpc(
    ClientControl::DoEchoRpcRequest.new(request: 'hello'))
  STDERR.puts 'killing client'
  Process.kill('SIGINT', client_pid)
  Process.wait(client_pid)
  client_exit_status = $CHILD_STATUS
  if client_exit_status.exited?
    if client_exit_status.exitstatus != 0
      STDERR.puts 'Client did not close gracefully'
      exit(1)
    end
  else
    STDERR.puts 'Client did not close gracefully'
    exit(1)
  end

  STDERR.puts 'Client ended gracefully'
  # no need to call cleanup, client should already be dead
  server_runner.stop
end

main
