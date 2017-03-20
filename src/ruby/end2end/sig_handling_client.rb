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

# Test client. Sends RPC's as normal but process also has signal handlers
class SigHandlingClientController < ClientControl::ClientController::Service
  def initialize(srv, stub)
    @srv = srv
    @stub = stub
  end

  def do_echo_rpc(req, _)
    response = @stub.echo(Echo::EchoRequest.new(request: req.request))
    fail 'bad response' unless response.response == req.request
    ClientControl::Void.new
  end

  def shutdown(_, _)
    Thread.new do
      # TODO(apolcyn) There is a race between stopping the
      # server and the "shutdown" rpc completing,
      # See if stop method on server can end active RPC cleanly, to
      # avoid this sleep.
      sleep 3
      @srv.stop
    end
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

  Signal.trap('TERM') do
    STDERR.puts 'SIGTERM received'
  end

  Signal.trap('INT') do
    STDERR.puts 'SIGINT received'
  end

  srv = GRPC::RpcServer.new
  srv.add_http2_port("0.0.0.0:#{client_control_port}",
                     :this_port_is_insecure)
  stub = Echo::EchoServer::Stub.new("localhost:#{server_port}",
                                    :this_channel_is_insecure)
  srv.handle(SigHandlingClientController.new(srv, stub))
  srv.run
end

main
