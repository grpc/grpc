#!/usr/bin/env ruby

# Copyright 2015, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

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
