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

# Attempt to reproduce
# https://github.com/GoogleCloudPlatform/google-cloud-ruby/issues/1327

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

  thd = Thread.new do
    stub = Echo::EchoServer::Stub.new("localhost:#{server_port}",
                                      :this_channel_is_insecure)
    stub.echo(Echo::EchoRequest.new(request: 'hello'))
    fail 'the clients rpc in this test shouldnt complete. ' \
      'expecting SIGINT to happen in the middle of the call'
  end
  thd.join
end

main
