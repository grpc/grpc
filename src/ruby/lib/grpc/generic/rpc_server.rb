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

require 'grpc/grpc'
require 'grpc/generic/active_call'
require 'grpc/generic/service'
require 'thread'
require 'xray/thread_dump_signal_handler'

# GRPC contains the General RPC module.
module GRPC
  # RpcServer hosts a number of services and makes them available on the
  # network.
  class RpcServer
    include Core::CompletionType
    include Core::TimeConsts
    extend ::Forwardable

    def_delegators :@server, :add_http2_port

    # Default thread pool size is 3
    DEFAULT_POOL_SIZE = 3

    # Default max_waiting_requests size is 20
    DEFAULT_MAX_WAITING_REQUESTS = 20

    # Creates a new RpcServer.
    #
    # The RPC server is configured using keyword arguments.
    #
    # There are some specific keyword args used to configure the RpcServer
    # instance, however other arbitrary are allowed and when present are used
    # to configure the listeninng connection set up by the RpcServer.
    #
    # * server_override: which if passed must be a [GRPC::Core::Server].  When
    # present.
    #
    # * poll_period: when present, the server polls for new events with this
    # period
    #
    # * pool_size: the size of the thread pool the server uses to run its
    # threads
    #
    # * completion_queue_override: when supplied, this will be used as the
    # completion_queue that the server uses to receive network events,
    # otherwise its creates a new instance itself
    #
    # * creds: [GRPC::Core::ServerCredentials]
    # the credentials used to secure the server
    #
    # * max_waiting_requests: the maximum number of requests that are not
    # being handled to allow. When this limit is exceeded, the server responds
    # with not available to new requests
    def initialize(pool_size:DEFAULT_POOL_SIZE,
                   max_waiting_requests:DEFAULT_MAX_WAITING_REQUESTS,
                   poll_period:INFINITE_FUTURE,
                   completion_queue_override:nil,
                   server_override:nil,
                   **kw)
      if completion_queue_override.nil?
        cq = Core::CompletionQueue.new
      else
        cq = completion_queue_override
        unless cq.is_a? Core::CompletionQueue
          fail(ArgumentError, 'not a CompletionQueue')
        end
      end
      @cq = cq

      if server_override.nil?
        srv = Core::Server.new(@cq, kw)
      else
        srv = server_override
        fail(ArgumentError, 'not a Server') unless srv.is_a? Core::Server
      end
      @server = srv

      @pool_size = pool_size
      @max_waiting_requests = max_waiting_requests
      @poll_period = poll_period
      @run_mutex = Mutex.new
      @run_cond = ConditionVariable.new
      @pool = Pool.new(@pool_size)
    end

    # stops a running server
    #
    # the call has no impact if the server is already stopped, otherwise
    # server's current call loop is it's last.
    def stop
      return unless @running
      @stopped = true
      @pool.stop
    end

    # determines if the server is currently running
    def running?
      @running ||= false
    end

    # Is called from other threads to wait for #run to start up the server.
    #
    # If run has not been called, this returns immediately.
    #
    # @param timeout [Numeric] number of seconds to wait
    # @result [true, false] true if the server is running, false otherwise
    def wait_till_running(timeout = 0.1)
      end_time, sleep_period = Time.now + timeout, (1.0 * timeout) / 100
      while Time.now < end_time
        @run_mutex.synchronize { @run_cond.wait(@run_mutex) } unless running?
        sleep(sleep_period)
      end
      running?
    end

    # determines if the server is currently stopped
    def stopped?
      @stopped ||= false
    end

    # handle registration of classes
    #
    # service is either a class that includes GRPC::GenericService and whose
    # #new function can be called without argument or any instance of such a
    # class.
    #
    # E.g, after
    #
    # class Divider
    #   include GRPC::GenericService
    #   rpc :div DivArgs, DivReply    # single request, single response
    #   def initialize(optional_arg='default option') # no args
    #     ...
    #   end
    #
    # srv = GRPC::RpcServer.new(...)
    #
    # # Either of these works
    #
    # srv.handle(Divider)
    #
    # # or
    #
    # srv.handle(Divider.new('replace optional arg'))
    #
    # It raises RuntimeError:
    # - if service is not valid service class or object
    # - its handler methods are already registered
    # - if the server is already running
    #
    # @param service [Object|Class] a service class or object as described
    #        above
    def handle(service)
      fail 'cannot add services if the server is running' if running?
      fail 'cannot add services if the server is stopped' if stopped?
      cls = service.is_a?(Class) ? service : service.class
      assert_valid_service_class(cls)
      add_rpc_descs_for(service)
    end

    # runs the server
    #
    # - if no rpc_descs are registered, this exits immediately, otherwise it
    #   continues running permanently and does not return until program exit.
    #
    # - #running? returns true after this is called, until #stop cause the
    #   the server to stop.
    def run
      if rpc_descs.size == 0
        logger.warn('did not run as no services were present')
        return
      end
      @run_mutex.synchronize do
        @running = true
        @run_cond.signal
      end
      @pool.start
      @server.start
      server_tag = Object.new
      until stopped?
        @server.request_call(server_tag)
        ev = @cq.pluck(server_tag, @poll_period)
        next if ev.nil?
        if ev.type != SERVER_RPC_NEW
          logger.warn("bad evt: got:#{ev.type}, want:#{SERVER_RPC_NEW}")
          ev.close
          next
        end
        c = new_active_server_call(ev.call, ev.result)
        unless c.nil?
          mth = ev.result.method.to_sym
          ev.close
          @pool.schedule(c) do |call|
            rpc_descs[mth].run_server_method(call, rpc_handlers[mth])
          end
        end
      end
      @running = false
    end

    def new_active_server_call(call, new_server_rpc)
      # Accept the call.  This is necessary even if a status is to be sent
      # back immediately
      finished_tag = Object.new
      call_queue = Core::CompletionQueue.new
      call.metadata = new_server_rpc.metadata  # store the metadata
      call.server_accept(call_queue, finished_tag)
      call.server_end_initial_metadata

      # Send UNAVAILABLE if there are too many unprocessed jobs
      jobs_count, max = @pool.jobs_waiting, @max_waiting_requests
      logger.info("waiting: #{jobs_count}, max: #{max}")
      if @pool.jobs_waiting > @max_waiting_requests
        logger.warn("NOT AVAILABLE: too many jobs_waiting: #{new_server_rpc}")
        noop = proc { |x| x }
        c = ActiveCall.new(call, call_queue, noop, noop,
                           new_server_rpc.deadline,
                           finished_tag: finished_tag)
        c.send_status(StatusCodes::UNAVAILABLE, '')
        return nil
      end

      # Send NOT_FOUND if the method does not exist
      mth = new_server_rpc.method.to_sym
      unless rpc_descs.key?(mth)
        logger.warn("NOT_FOUND: #{new_server_rpc}")
        noop = proc { |x| x }
        c = ActiveCall.new(call, call_queue, noop, noop,
                           new_server_rpc.deadline,
                           finished_tag: finished_tag)
        c.send_status(StatusCodes::NOT_FOUND, '')
        return nil
      end

      # Create the ActiveCall
      rpc_desc = rpc_descs[mth]
      logger.info("deadline is #{new_server_rpc.deadline}; (now=#{Time.now})")
      ActiveCall.new(call, call_queue,
                     rpc_desc.marshal_proc, rpc_desc.unmarshal_proc(:input),
                     new_server_rpc.deadline, finished_tag: finished_tag)
    end

    # Pool is a simple thread pool for running server requests.
    class Pool
      def initialize(size)
        fail 'pool size must be positive' unless size > 0
        @jobs = Queue.new
        @size = size
        @stopped = false
        @stop_mutex = Mutex.new
        @stop_cond = ConditionVariable.new
        @workers = []
      end

      # Returns the number of jobs waiting
      def jobs_waiting
        @jobs.size
      end

      # Runs the given block on the queue with the provided args.
      #
      # @param args the args passed blk when it is called
      # @param blk the block to call
      def schedule(*args, &blk)
        fail 'already stopped' if @stopped
        return if blk.nil?
        logger.info('schedule another job')
        @jobs << [blk, args]
      end

      # Starts running the jobs in the thread pool.
      def start
        fail 'already stopped' if @stopped
        until @workers.size == @size.to_i
          next_thread = Thread.new do
            catch(:exit) do  # allows { throw :exit } to kill a thread
              loop do
                begin
                  blk, args = @jobs.pop
                  blk.call(*args)
                rescue StandardError => e
                  logger.warn('Error in worker thread')
                  logger.warn(e)
                end
              end
            end

            # removes the threads from workers, and signal when all the
            # threads are complete.
            @stop_mutex.synchronize do
              @workers.delete(Thread.current)
              @stop_cond.signal if @workers.size == 0
            end
          end
          @workers << next_thread
        end
      end

      # Stops the jobs in the pool
      def stop
        logger.info('stopping, will wait for all the workers to exit')
        @workers.size.times { schedule { throw :exit } }
        @stopped = true

        # TODO: allow configuration of the keepalive period
        keep_alive = 5
        @stop_mutex.synchronize do
          @stop_cond.wait(@stop_mutex, keep_alive) if @workers.size > 0
        end

        # Forcibly shutdown any threads that are still alive.
        if @workers.size > 0
          logger.warn("forcibly terminating #{@workers.size} worker(s)")
          @workers.each do |t|
            next unless t.alive?
            begin
              t.exit
            rescue StandardError => e
              logger.warn('error while terminating a worker')
              logger.warn(e)
            end
          end
        end

        logger.info('stopped, all workers are shutdown')
      end
    end

    protected

    def rpc_descs
      @rpc_descs ||= {}
    end

    def rpc_handlers
      @rpc_handlers ||= {}
    end

    private

    def assert_valid_service_class(cls)
      unless cls.include?(GenericService)
        fail "#{cls} should 'include GenericService'"
      end
      if cls.rpc_descs.size == 0
        fail "#{cls} should specify some rpc descriptions"
      end
      cls.assert_rpc_descs_have_methods
    end

    def add_rpc_descs_for(service)
      cls = service.is_a?(Class) ? service : service.class
      specs = rpc_descs
      handlers = rpc_handlers
      cls.rpc_descs.each_pair do |name, spec|
        route = "/#{cls.service_name}/#{name}".to_sym
        if specs.key? route
          fail "Cannot add rpc #{route} from #{spec}, already registered"
        else
          specs[route] = spec
          if service.is_a?(Class)
            handlers[route] = cls.new.method(name.to_s.underscore.to_sym)
          else
            handlers[route] = service.method(name.to_s.underscore.to_sym)
          end
          logger.info("handling #{route} with #{handlers[route]}")
        end
      end
    end
  end
end
