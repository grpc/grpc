#!/usr/bin/env ruby

# Copyright 2016 gRPC authors.
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

# Service that sleeps for a long time upon receiving an 'echo request'
# Also, this calls it's callback upon receiving an RPC as a method
# of synchronization/waiting for the child to start.
class SleepingEchoServerImpl < Echo::EchoServer::Service
  def initialize(received_rpc_callback)
    @received_rpc_callback = received_rpc_callback
  end

  def echo(echo_req, _)
    @received_rpc_callback.call
    # sleep forever to get the client stuck waiting
    sleep
    Echo::EchoReply.new(response: echo_req.request)
  end
end

def main
  STDERR.puts 'start server'

  received_rpc = false
  received_rpc_mu = Mutex.new
  received_rpc_cv = ConditionVariable.new
  received_rpc_callback = proc do
    received_rpc_mu.synchronize do
      received_rpc = true
      received_rpc_cv.signal
    end
  end

  service_impl = SleepingEchoServerImpl.new(received_rpc_callback)
  # RPCs against the server will all be freezing, so kill thread
  # pool workers immediately rather than after waiting for a second.
  rpc_server_args = { poll_period: 0, pool_keep_alive: 0 }
  server_runner = ServerRunner.new(service_impl, rpc_server_args: rpc_server_args)
  server_port = server_runner.run
  STDERR.puts 'start client'
  client_controller = ClientController.new(
    'killed_client_thread_client.rb', server_port)

  received_rpc_mu.synchronize do
    received_rpc_cv.wait(received_rpc_mu) until received_rpc
  end

  # SIGTERM the child process now that it's
  # in the middle of an RPC (happening on a non-main thread)
  Process.kill('SIGTERM', client_controller.client_pid)
  STDERR.puts 'sent shutdown'

  begin
    Timeout.timeout(10) do
      Process.wait(client_controller.client_pid)
    end
  rescue Timeout::Error
    STDERR.puts "timeout wait for client pid #{client_controller.client_pid}"
    Process.kill('SIGKILL', client_controller.client_pid)
    Process.wait(client_controller.client_pid)
    STDERR.puts 'killed client child'
    raise 'Timed out waiting for client process. ' \
      'It likely freezes when killed while in the middle of an rpc'
  end

  client_exit_code = $CHILD_STATUS
  if client_exit_code.termsig != 15 # SIGTERM
    fail 'expected client exit from SIGTERM ' \
      "but got child status: #{client_exit_code}"
  end

  server_runner.stop
end

main
