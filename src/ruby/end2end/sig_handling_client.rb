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
