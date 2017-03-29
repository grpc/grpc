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

require_relative 'grpc'

module GRPC
  # Provides calls to allow forking
  class Fork
    def self.prefork
      GRPC::Core::Fork.prefork
    end

    def self.postfork_parent
      GRPC::Core::Fork.postfork_parent
    end

    def self.postfork_child
      GRPC::Core::Fork.postfork_child
    end
  end

  # Class containing details of a forked process
  class ForkedServerProcess
    def initialize(child_pid, wr)
      @child_pid = child_pid
      @wr = wr
    end

    def shutdown
      @wr.write('\n')
      @wr.flush
      Process.waitpid(@child_pid)
      @wr.close
    end
  end

  # Similar to regular server, but forks processes for parallelism
  class PreforkServer
    extend ::Forwardable

    def_delegators :@server, :wait_till_running, :handle, :add_http2_port

    # Default process pool count
    DEFAULT_PROCESS_COUNT = 10

    def initialize(process_count:DEFAULT_PROCESS_COUNT,
                   pool_size:GRPC::RpcServer::DEFAULT_POOL_SIZE,
                   max_waiting_requests:\
                      GRPC::RpcServer::DEFAULT_MAX_WAITING_REQUESTS,
                   poll_period:GRPC::RpcServer::DEFAULT_POLL_PERIOD,
                   connect_md_proc:nil,
                   server_args:{})
      @process_count = process_count
      @server = GRPC::RpcServer.new(pool_size: pool_size,
                                    max_waiting_requests: max_waiting_requests,
                                    poll_period: poll_period,
                                    connect_md_proc: connect_md_proc,
                                    server_args: server_args)
      @child_processes = []
    end

    def run
      (@process_count - 1).times do
        rd, wr = IO.pipe
        Fork.prefork
        child_pid = fork
        if !child_pid.nil?
          Fork.postfork_parent
          rd.close
          @child_processes.push(ForkedServerProcess.new(child_pid, wr))
        else
          begin
            Fork.postfork_child
            wr.close
            server_thread = Thread.new { @server.run_till_terminated }
            rd.read(1)
            rd.close
            @server.stop
            server_thread.join
          rescue => e # rubocop:disable HandleExceptions
            GRPC.logger.error(e.message)
            GRPC.logger.error(e.backtrace.join("\n"))
          ensure
            # Avoid calling exit handlers from the child process
            Kernal.exit!
          end
        end
      end
      # Run a server in the parent process as well.  This is neccesary,
      # the parent process is the only one that can shutdown() a shared
      # listening socket (by design)
      @server.run
    end

    def stop
      @server.stop
      @child_processes.each(&:shutdown)
    end

    alias_method :run_till_terminated, :run
  end
end
