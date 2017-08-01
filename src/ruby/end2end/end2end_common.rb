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

this_dir = File.expand_path(File.dirname(__FILE__))
protos_lib_dir = File.join(this_dir, 'lib')
grpc_lib_dir = File.join(File.dirname(this_dir), 'lib')
$LOAD_PATH.unshift(grpc_lib_dir) unless $LOAD_PATH.include?(grpc_lib_dir)
$LOAD_PATH.unshift(protos_lib_dir) unless $LOAD_PATH.include?(protos_lib_dir)
$LOAD_PATH.unshift(this_dir) unless $LOAD_PATH.include?(this_dir)

require 'grpc'
require 'echo_services_pb'
require 'client_control_services_pb'
require 'socket'
require 'optparse'
require 'thread'
require 'timeout'
require 'English' # see https://github.com/bbatsov/rubocop/issues/1747

# GreeterServer is simple server that implements the Helloworld Greeter server.
class EchoServerImpl < Echo::EchoServer::Service
  # say_hello implements the SayHello rpc method.
  def echo(echo_req, _)
    Echo::EchoReply.new(response: echo_req.request)
  end
end

# ServerRunner starts an "echo server" that test clients can make calls to
class ServerRunner
  def initialize(service_impl)
    @service_impl = service_impl
  end

  def run
    @srv = GRPC::RpcServer.new
    port = @srv.add_http2_port('0.0.0.0:0', :this_port_is_insecure)
    @srv.handle(@service_impl)

    @thd = Thread.new do
      @srv.run
    end
    @srv.wait_till_running
    port
  end

  def stop
    @srv.stop
    @thd.join
    fail 'server not stopped' unless @srv.stopped?
  end
end

def start_client(client_main, server_port)
  this_dir = File.expand_path(File.dirname(__FILE__))

  tmp_server = TCPServer.new(0)
  client_control_port = tmp_server.local_address.ip_port
  tmp_server.close

  client_path = File.join(this_dir, client_main)
  client_pid = Process.spawn(RbConfig.ruby,
                             client_path,
                             "--client_control_port=#{client_control_port}",
                             "--server_port=#{server_port}")
  sleep 1
  control_stub = ClientControl::ClientController::Stub.new(
    "localhost:#{client_control_port}", :this_channel_is_insecure)
  [control_stub, client_pid]
end

def cleanup(control_stub, client_pid, server_runner)
  control_stub.shutdown(ClientControl::Void.new)
  Process.wait(client_pid)

  client_exit_code = $CHILD_STATUS

  if client_exit_code != 0
    fail "term sig test failure: client exit code: #{client_exit_code}"
  end

  server_runner.stop
end
