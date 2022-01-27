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
  client_controller = ClientController.new(
    'sig_handling_client.rb', server_port)
  count = 0
  while count < 5
    client_controller.stub.do_echo_rpc(
      ClientControl::DoEchoRpcRequest.new(request: 'hello'))
    Process.kill('SIGTERM', client_controller.client_pid)
    Process.kill('SIGINT', client_controller.client_pid)
    count += 1
  end
  client_controller.stub.shutdown(ClientControl::Void.new)
  Process.wait(client_controller.client_pid)
  fail "client exit code: #{$CHILD_STATUS}" unless $CHILD_STATUS.to_i.zero?
  server_runner.stop
end

main
