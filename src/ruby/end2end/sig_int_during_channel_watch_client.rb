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

require_relative './end2end_common'

# Start polling the channel state in both the main thread
# and a child thread. Try to get the driver to send process-ending
# interrupt while both a child thread and the main thread are in the
# middle of a blocking connectivity_state call.
def main
  server_port = ''
  OptionParser.new do |opts|
    opts.on('--client_control_port=P', String) do
      STDERR.puts 'client_control_port not used'
    end
    opts.on('--server_port=P', String) do |p|
      server_port = p
    end
  end.parse!

  trap('SIGINT') { exit 0 }

  thd = Thread.new do
    child_thread_channel = GRPC::Core::Channel.new("localhost:#{server_port}",
                                                   {},
                                                   :this_channel_is_insecure)
    loop do
      state = child_thread_channel.connectivity_state(false)
      child_thread_channel.watch_connectivity_state(state, Time.now + 360)
    end
  end

  main_channel = GRPC::Core::Channel.new("localhost:#{server_port}",
                                         {},
                                         :this_channel_is_insecure)
  loop do
    state = main_channel.connectivity_state(false)
    main_channel.watch_connectivity_state(state, Time.now + 360)
  end

  thd.join
end

main
