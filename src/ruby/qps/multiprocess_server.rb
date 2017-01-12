# Copyright 2017, Google Inc.
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

require 'socket'
require 'facter'

this_dir = File.expand_path(File.dirname(__FILE__))
worker_path = File.join(this_dir, "multiprocess_server_worker.rb")

class BenchmarkMultiprocessServer
  def initialize(config, port)
    @worker_pids = []
    if port == 0
      server = TCPServer.new 0
      @port = server.local_address.ip_port
      server.close
    else
      @port = port
    end
    worker_args = ["--port=#{@port}"]
    if config.security_params
      worker_args.push('--tls')
    end
    this_dir = File.expand_path(File.dirname(__FILE__))
    worker_path = File.join(this_dir, "multiprocess_server_worker.rb")
    @start_time = Time.now
    process_count = config.async_server_threads
    if process_count == 0
      process_count = Facter.value('processors')['count']
    end
    process_count.times do
      @worker_pids.push(Process.spawn(RbConfig.ruby, worker_path, *worker_args))
    end
  end

  def mark(reset)
    s = Grpc::Testing::ServerStats.new(time_elapsed:
                                       (Time.now-@start_time).to_f)
    @start_time = Time.now if reset
    s
  end

  def get_port
    @port
  end

  def stop
    @worker_pids.each do |pid|
      Process.kill('USR1', pid)
      Process.wait(pid)
    end
  end
end
