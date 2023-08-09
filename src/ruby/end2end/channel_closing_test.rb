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

# make sure that the client doesn't freeze when channel is closed
# explicitly while it's used

require_relative './end2end_common'

def main
  STDERR.puts 'start server'
  server_runner = ServerRunner.new(EchoServerImpl)
  server_port = server_runner.run
  STDERR.puts 'start client'
  client_controller = ClientController.new(
    'channel_closing_client.rb', server_port)
  # sleep to allow time for the client to get into
  # the middle of a "watch connectivity state" call
  sleep 3

  begin
    Timeout.timeout(120) do
      loop do
        begin
          client_controller.stub.shutdown(ClientControl::Void.new)
          break
        rescue GRPC::BadStatus => e
          STDERR.puts "control_stub.shutdown RPC received error:|#{e}|. " \
          "This could mean that that child process e.g. isn't running yet, " \
          "so we'll retry the RPC"
        end
      end
      Process.wait(client_controller.client_pid)
    end
  rescue Timeout::Error
    STDERR.puts "timeout wait for client pid #{client_controller.client_pid}"
    Process.kill('SIGKILL', client_controller.client_pid)
    Process.wait(client_controller.client_pid)
    STDERR.puts 'killed client child'
    raise 'Timed out waiting for client process. It likely freezes when a ' \
      'channel is closed while connectivity is watched'
  end

  client_exit_code = $CHILD_STATUS
  if client_exit_code != 0
    fail "channel closing client failed, exit code #{client_exit_code}"
  end

  server_runner.stop
end

main
