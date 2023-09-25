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
require_relative '../spec/support/helpers'

include GRPC::Spec::Helpers

# Useful to update a value within a do block
class MutableValue
  attr_accessor :value

  def initialize(value)
    @value = value
  end
end

class EchoServerImpl < Echo::EchoServer::Service
  def echo(echo_req, _)
    Echo::EchoReply.new(response: echo_req.request)
  end
end

class SecureEchoServerImpl < Echo::EchoServer::Service
  def echo(echo_req, call)
    unless call.metadata["authorization"] == 'test'
      fail "expected authorization header with value: test"
    end
    Echo::EchoReply.new(response: echo_req.request)
  end
end

# ServerRunner starts an "echo server" that test clients can make calls to
class ServerRunner
  attr_accessor :server_creds

  def initialize(service_impl, rpc_server_args: {})
    @service_impl = service_impl
    @rpc_server_args = rpc_server_args
    @server_creds = :this_port_is_insecure
  end

  def run
    @srv = new_rpc_server_for_testing(@rpc_server_args)
    port = @srv.add_http2_port('0.0.0.0:0', @server_creds)
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

# ClientController is used to start a child process and communicate
# with it for test orchestration purposes via RPCs.
class ClientController < ClientControl::ParentController::Service
  attr_reader :stub, :client_pid

  def initialize(client_main, server_port)
    this_dir = File.expand_path(File.dirname(__FILE__))
    client_path = File.join(this_dir, client_main)
    @server = new_rpc_server_for_testing(poll_period: 3)
    port = @server.add_http2_port('localhost:0', :this_port_is_insecure)
    server_thread = Thread.new do
      @server.handle(self)
      @server.run
    end
    @server.wait_till_running
    @client_controller_port_mu = Mutex.new
    @client_controller_port_cv = ConditionVariable.new
    @client_controller_port = nil
    @client_pid = Process.spawn(RbConfig.ruby,
                                client_path,
                                "--parent_controller_port=#{port}",
                                "--server_port=#{server_port}")
    begin
      Timeout.timeout(60) do
        @client_controller_port_mu.synchronize do
          while @client_controller_port.nil?
            @client_controller_port_cv.wait(@client_controller_port_mu)
          end
        end
      end
    rescue => e
      fail "timeout waiting for child process to report port. error: #{e}"
    end
    @server.stop
    server_thread.join
    @stub = ClientControl::ClientController::Stub.new(
      "localhost:#{@client_controller_port}", :this_channel_is_insecure)
  end

  def set_client_controller_port(req, _)
    @client_controller_port_mu.synchronize do
      unless @client_controller_port.nil?
        fail 'client controller port already set'
      end
      @client_controller_port = req.port
      @client_controller_port_cv.broadcast
    end
    ClientControl::Void.new
  end
end

def report_controller_port_to_parent(parent_controller_port, client_controller_port)
  unless parent_controller_port.to_i > 0
    fail "bad parent control port: |#{parent_controller_port}|"
  end
  stub = ClientControl::ParentController::Stub.new(
    "localhost:#{parent_controller_port.to_i}", :this_channel_is_insecure)
  m = ClientControl::Port.new
  m.port = client_controller_port.to_i
  stub.set_client_controller_port(m, deadline: Time.now + 10)
end

def with_logging(action)
  STDERR.puts "#{action}: begin (pid=#{Process.pid})"
  yield
  STDERR.puts "#{action}: done (pid=#{Process.pid})"
end
