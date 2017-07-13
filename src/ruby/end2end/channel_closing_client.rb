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

# Calls '#close' on a Channel when "shutdown" called. This tries to
# trigger a hang or crash bug by closing a channel actively being watched
class ChannelClosingClientController < ClientControl::ClientController::Service
  def initialize(ch)
    @ch = ch
  end

  def shutdown(_, _)
    @ch.close
    ClientControl::Void.new
  end
end

def main
  client_control_port = ''
  server_port = ''
  OptionParser.new do |opts|
    opts.on('--client_control_port=P', String) do |p|
      client_control_port = p
    end
    opts.on('--server_port=P', String) do |p|
      server_port = p
    end
  end.parse!

  ch = GRPC::Core::Channel.new("localhost:#{server_port}", {},
                               :this_channel_is_insecure)

  srv = GRPC::RpcServer.new
  thd = Thread.new do
    srv.add_http2_port("0.0.0.0:#{client_control_port}", :this_port_is_insecure)
    srv.handle(ChannelClosingClientController.new(ch))
    srv.run
  end

  # this should break out with an exception once the channel is closed
  loop do
    begin
      state = ch.connectivity_state(true)
      ch.watch_connectivity_state(state, Time.now + 360)
    rescue RuntimeError => e
      STDERR.puts "(expected) error occurred: #{e.inspect}"
      break
    end
  end

  srv.stop
  thd.join
end

main
