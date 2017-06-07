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
  server_runner = ServerRunner.new(EchoServerImpl)
  server_port = server_runner.run

  sleep 1

  STDERR.puts 'start client'
  control_stub, client_pid = start_client('sig_handling_client.rb', server_port)

  sleep 1

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
