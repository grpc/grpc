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
# Also, this notifies @call_started_cv once it has received a request.
class SleepingEchoServerImpl < Echo::EchoServer::Service
  def initialize(call_started, call_started_mu, call_started_cv)
    @call_started = call_started
    @call_started_mu = call_started_mu
    @call_started_cv = call_started_cv
  end

  def echo(echo_req, _)
    @call_started_mu.synchronize do
      @call_started.set_true
      @call_started_cv.signal
    end
    sleep 1000
    Echo::EchoReply.new(response: echo_req.request)
  end
end

# Mutable boolean
class BoolHolder
  attr_reader :val

  def init
    @val = false
  end

  def set_true
    @val = true
  end
end

def main
  STDERR.puts 'start server'

  call_started = BoolHolder.new
  call_started_mu = Mutex.new
  call_started_cv = ConditionVariable.new

  service_impl = SleepingEchoServerImpl.new(call_started,
                                            call_started_mu,
                                            call_started_cv)
  server_runner = ServerRunner.new(service_impl)
  server_port = server_runner.run

  STDERR.puts 'start client'
  _, client_pid = start_client('killed_client_thread_client.rb',
                               server_port)

  call_started_mu.synchronize do
    call_started_cv.wait(call_started_mu) until call_started.val
  end

  # SIGTERM the child process now that it's
  # in the middle of an RPC (happening on a non-main thread)
  Process.kill('SIGTERM', client_pid)
  STDERR.puts 'sent shutdown'

  begin
    Timeout.timeout(10) do
      Process.wait(client_pid)
    end
  rescue Timeout::Error
    STDERR.puts "timeout wait for client pid #{client_pid}"
    Process.kill('SIGKILL', client_pid)
    Process.wait(client_pid)
    STDERR.puts 'killed client child'
    raise 'Timed out waiting for client process. ' \
      'It likely hangs when killed while in the middle of an rpc'
  end

  client_exit_code = $CHILD_STATUS
  if client_exit_code.termsig != 15 # SIGTERM
    fail 'expected client exit from SIGTERM ' \
      "but got child status: #{client_exit_code}"
  end

  server_runner.stop
end

main
