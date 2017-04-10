#!/usr/bin/env ruby

# Copyright 2016, Google Inc.
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

  # SIGINT the child process now that it's
  # in the middle of an RPC (happening on a non-main thread)
  Process.kill('SIGINT', client_pid)
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
  if client_exit_code.termsig != 2 # SIGINT
    fail 'expected client exit from SIGINT ' \
      "but got child status: #{client_exit_code}"
  end

  server_runner.stop
end

main
