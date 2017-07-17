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

# make sure that the client doesn't hang when process ended abruptly

require_relative './end2end_common'

def main
  STDERR.puts 'start server'
  server_runner = ServerRunner.new(EchoServerImpl)
  server_port = server_runner.run

  sleep 1

  STDERR.puts 'start client'
  _, client_pid = start_client('channel_state_client.rb', server_port)

  sleep 3

  Process.kill('SIGTERM', client_pid)

  begin
    Timeout.timeout(10) { Process.wait(client_pid) }
  rescue Timeout::Error
    STDERR.puts "timeout wait for client pid #{client_pid}"
    Process.kill('SIGKILL', client_pid)
    Process.wait(client_pid)
    STDERR.puts 'killed client child'
    raise 'Timed out waiting for client process. ' \
           'It likely hangs when ended abruptly'
  end

  # The interrupt in the child process should cause it to
  # exit a non-zero status, so don't check it here.
  # This test mainly tries to catch deadlock.
  server_runner.stop
end

main
