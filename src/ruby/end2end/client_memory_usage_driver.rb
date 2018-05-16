#!/usr/bin/env ruby

# Copyright 2018 gRPC authors.
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
  STDERR.puts 'start client'
  _, client_pid = start_client('client_memory_usage_client.rb', server_port)

  Process.wait(client_pid)

  client_exit_code = $CHILD_STATUS
  if client_exit_code != 0
    raise "Getting memory usage was failed, exit code #{client_exit_code}"
  end
ensure
  server_runner.stop
end

main
