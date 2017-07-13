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

require_relative './end2end_common'

def main
  STDERR.puts 'start server'
  server_runner = ServerRunner.new(EchoServerImpl)
  server_port = server_runner.run

  # TODO(apolcyn) Can we get rid of this sleep?
  # Without it, an immediate call to the just started EchoServer
  # fails with UNAVAILABLE
  sleep 1

  STDERR.puts 'start client'
  _, client_pid = start_client('forking_client_client.rb',
                               server_port)

  begin
    Timeout.timeout(10) do
      Process.wait(client_pid)
    end
  rescue Timeout::Error
    STDERR.puts "timeout wait for client pid #{client_pid}"
    Process.kill('SIGKILL', client_pid)
    Process.wait(client_pid)
    STDERR.puts 'killed client child'
    raise 'Timed out waiting for client process. ' \
      'It likely hangs when requiring grpc, then forking, then using grpc '
  end

  client_exit_code = $CHILD_STATUS
  if client_exit_code != 0
    fail "forking client client failed, exit code #{client_exit_code}"
  end

  server_runner.stop
end

main
