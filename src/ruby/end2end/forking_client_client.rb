#!/usr/bin/env ruby

# Copyright 2015 gRPC authors.
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

# Prompted by and minimal repro of https://github.com/grpc/grpc/issues/10658

require_relative './end2end_common'

def main
  server_port = ''
  OptionParser.new do |opts|
    opts.on('--client_control_port=P', String) do
      STDERR.puts 'client control port not used'
    end
    opts.on('--server_port=P', String) do |p|
      server_port = p
    end
  end.parse!

  p = fork do
    stub = Echo::EchoServer::Stub.new("localhost:#{server_port}",
                                      :this_channel_is_insecure)
    stub.echo(Echo::EchoRequest.new(request: 'hello'))
  end

  begin
    Timeout.timeout(10) do
      Process.wait(p)
    end
  rescue Timeout::Error
    STDERR.puts "timeout waiting for forked process #{p}"
    Process.kill('SIGKILL', p)
    Process.wait(p)
    raise 'Timed out waiting for client process. ' \
      'It likely hangs when using gRPC after loading it and then forking'
  end

  client_exit_code = $CHILD_STATUS
  fail "forked process failed #{client_exit_code}" if client_exit_code != 0
end

main
