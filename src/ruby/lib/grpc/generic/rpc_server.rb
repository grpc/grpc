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

require_relative '../grpc'
require_relative 'active_call'
require_relative 'service'
require 'thread'

# A global that contains signals the gRPC servers should respond to.
$grpc_signals = []

# GRPC contains the General RPC module.
module GRPC
  # Handles the signals in $grpc_signals.
  #
  # @return false if the server should exit, true if not.
  def handle_signals
    loop do
      sig = $grpc_signals.shift
      case sig
      when 'INT'
        return false
      when 'TERM'
        return false
      when nil
        return true
      end
    end
    true
  end
  module_function :handle_signals

  # Sets up a signal handler that adds signals to the signal handling global.
  #
  # Signal handlers should do as little as humanly possible.
  # Here, they just add themselves to $grpc_signals
  #
  # RpcServer (and later other parts of gRPC) monitors the signals
  # $grpc_signals in its own non-signal context.
  def trap_signals
    %w(INT TERM).each { |sig| trap(sig) { $grpc_signals << sig } }
  end
  module_function :trap_signals

  # Pool is a simple thread pool.
  class Pool
    # Default keep alive period is 1s
    DEFAULT_KEEP_ALIVE = 1

    def initialize(size, keep_alive: DEFAULT_KEEP_ALIVE)
      fail 'pool size must be positive' unless size > 0
      @jobs = Queue.new
      @size = size
      @stopped = false
      @stop_mutex = Mutex.new # needs to be held when accessing @stopped
      @stop_cond = ConditionVariable.new
      @workers = []
      @keep_alive = keep_alive
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
      return if blk.nil?
      @stop_mutex.synchronize do
        if @stopped
          GRPC.logger.warn('did not schedule job, already stopped')
          return
        end
        GRPC.logger.info('schedule another job')
        @jobs << [blk, args]
      end
    end

    # Starts running the jobs in the thread pool.
    def start
      @stop_mutex.synchronize do
        fail 'already stopped' if @stopped
      end
      until @workers.size == @size.to_i
        next_thread = Thread.new do
          catch(:exit) do  # allows { throw :exit } to kill a thread
            loop_execute_jobs
          end
          remove_current_thread
        end
        @workers << next_thread
      end
    end

    # Stops the jobs in the pool
    def stop
      GRPC.logger.info('stopping, will wait for all the workers to exit')
      @workers.size.times { schedule { throw :exit } }
      @stop_mutex.synchronize do  # wait @keep_alive for works to stop
        @stopped = true
        @stop_cond.wait(@stop_mutex, @keep_alive) if @workers.size > 0
      end
      forcibly_stop_workers
      GRPC.logger.info('stopped, all workers are shutdown')
    end

    protected

    # Forcibly shutdown any threads that are still alive.
    def forcibly_stop_workers
      return unless @workers.size > 0
      GRPC.logger.info("forcibly terminating #{@workers.size} worker(s)")
      @workers.each do |t|
        next unless t.alive?
        begin
          t.exit
        rescue StandardError => e
          GRPC.logger.warn('error while terminating a worker')
          GRPC.logger.warn(e)
        end
      end
    end

    # removes the threads from workers, and signal when all the
    # threads are complete.
    def remove_current_thread
      @stop_mutex.synchronize do
        @workers.delete(Thread.current)
        @stop_cond.signal if @workers.size.zero?
      end
    end

    def loop_execute_jobs
      loop do
        begin
          blk, args = @jobs.pop
          blk.call(*args)
        rescue StandardError => e
          GRPC.logger.warn('Error in worker thread')
          GRPC.logger.warn(e)
        end
      end
    end
  end

  # RpcServer hosts a number of services and makes them available on the
  # network.
  class RpcServer
    include Core::CallOps
    include Core::TimeConsts
    extend ::Forwardable

    def_delegators :@server, :add_http2_port

    # Default thread pool size is 3
    DEFAULT_POOL_SIZE = 3

    # Default max_waiting_requests size is 20
    DEFAULT_MAX_WAITING_REQUESTS = 20

    # Default poll period is 1s
    DEFAULT_POLL_PERIOD = 1

    # Signal check period is 0.25s
    SIGNAL_CHECK_PERIOD = 0.25

    # setup_cq is used by #initialize to constuct a Core::CompletionQueue from
    # its arguments.
    def self.setup_cq(alt_cq)
      return Core::CompletionQueue.new if alt_cq.nil?
      unless alt_cq.is_a? Core::CompletionQueue
        fail(TypeError, '!CompletionQueue')
      end
      alt_cq
    end

    # setup_srv is used by #initialize to constuct a Core::Server from its
    # arguments.
    def self.setup_srv(alt_srv, cq, **kw)
      return Core::Server.new(cq, kw) if alt_srv.nil?
      fail(TypeError, '!Server') unless alt_srv.is_a? Core::Server
      alt_srv
    end

    # setup_connect_md_proc is used by #initialize to validate the
    # connect_md_proc.
    def self.setup_connect_md_proc(a_proc)
      return nil if a_proc.nil?
      fail(TypeError, '!Proc') unless a_proc.is_a? Proc
      a_proc
    end

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
    #
    # * connect_md_proc:
    # when non-nil is a proc for determining metadata to to send back the client
    # on receiving an invocation req.  The proc signature is:
    # {key: val, ..} func(method_name, {key: val, ...})
    def initialize(pool_size:DEFAULT_POOL_SIZE,
                   max_waiting_requests:DEFAULT_MAX_WAITING_REQUESTS,
                   poll_period:DEFAULT_POLL_PERIOD,
                   completion_queue_override:nil,
                   server_override:nil,
                   connect_md_proc:nil,
                   **kw)
      @connect_md_proc = RpcServer.setup_connect_md_proc(connect_md_proc)
      @cq = RpcServer.setup_cq(completion_queue_override)
      @max_waiting_requests = max_waiting_requests
      @poll_period = poll_period
      @pool_size = pool_size
      @pool = Pool.new(@pool_size)
      @run_cond = ConditionVariable.new
      @run_mutex = Mutex.new
      # running_state can take 4 values: :not_started, :running, :stopping, and
      # :stopped. State transitions can only proceed in that order.
      @running_state = :not_started
      @server = RpcServer.setup_srv(server_override, @cq, **kw)
    end

    # stops a running server
    #
    # the call has no impact if the server is already stopped, otherwise
    # server's current call loop is it's last.
    def stop
      @run_mutex.synchronize do
        fail 'Cannot stop before starting' if @running_state == :not_started
        return if @running_state != :running
        transition_running_state(:stopping)
      end
      deadline = from_relative_time(@poll_period)
      @server.close(@cq, deadline)
      @pool.stop
    end

    def running_state
      @run_mutex.synchronize do
        return @running_state
      end
    end

    # Can only be called while holding @run_mutex
    def transition_running_state(target_state)
      state_transitions = {
        not_started: :running,
        running: :stopping,
        stopping: :stopped
      }
      if state_transitions[@running_state] == target_state
        @running_state = target_state
      else
        fail "Bad server state transition: #{@running_state}->#{target_state}"
      end
    end

    def running?
      running_state == :running
    end

    def stopped?
      running_state == :stopped
    end

    # Is called from other threads to wait for #run to start up the server.
    #
    # If run has not been called, this returns immediately.
    #
    # @param timeout [Numeric] number of seconds to wait
    # @result [true, false] true if the server is running, false otherwise
    def wait_till_running(timeout = nil)
      @run_mutex.synchronize do
        @run_cond.wait(@run_mutex, timeout) if @running_state == :not_started
        return @running_state == :running
      end
    end

    # Runs the server in its own thread, then waits for signal INT or TERM on
    # the current thread to terminate it.
    def run_till_terminated
      GRPC.trap_signals
      t = Thread.new do
        run
      end
      t.abort_on_exception = true
      wait_till_running
      until running_state == :stopped
        sleep SIGNAL_CHECK_PERIOD
        break unless GRPC.handle_signals
      end
      stop
      t.join
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
      @run_mutex.synchronize do
        unless @running_state == :not_started
          fail 'cannot add services if the server has been started'
        end
        cls = service.is_a?(Class) ? service : service.class
        assert_valid_service_class(cls)
        add_rpc_descs_for(service)
      end
    end

    # runs the server
    #
    # - if no rpc_descs are registered, this exits immediately, otherwise it
    #   continues running permanently and does not return until program exit.
    #
    # - #running? returns true after this is called, until #stop cause the
    #   the server to stop.
    def run
      @run_mutex.synchronize do
        fail 'cannot run without registering services' if rpc_descs.size.zero?
        @pool.start
        @server.start
        transition_running_state(:running)
        @run_cond.broadcast
      end
      loop_handle_server_calls
    end

    # Sends RESOURCE_EXHAUSTED if there are too many unprocessed jobs
    def available?(an_rpc)
      jobs_count, max = @pool.jobs_waiting, @max_waiting_requests
      GRPC.logger.info("waiting: #{jobs_count}, max: #{max}")
      return an_rpc if @pool.jobs_waiting <= @max_waiting_requests
      GRPC.logger.warn("NOT AVAILABLE: too many jobs_waiting: #{an_rpc}")
      noop = proc { |x| x }
      c = ActiveCall.new(an_rpc.call, @cq, noop, noop, an_rpc.deadline)
      c.send_status(GRPC::Core::StatusCodes::RESOURCE_EXHAUSTED, '')
      nil
    end

    # Sends UNIMPLEMENTED if the method is not implemented by this server
    def implemented?(an_rpc)
      mth = an_rpc.method.to_sym
      return an_rpc if rpc_descs.key?(mth)
      GRPC.logger.warn("UNIMPLEMENTED: #{an_rpc}")
      noop = proc { |x| x }
      c = ActiveCall.new(an_rpc.call, @cq, noop, noop, an_rpc.deadline)
      c.send_status(GRPC::Core::StatusCodes::UNIMPLEMENTED, '')
      nil
    end

    # handles calls to the server
    def loop_handle_server_calls
      fail 'not started' if running_state == :not_started
      loop_tag = Object.new
      while running_state == :running
        begin
          an_rpc = @server.request_call(@cq, loop_tag, INFINITE_FUTURE)
          break if (!an_rpc.nil?) && an_rpc.call.nil?
          active_call = new_active_server_call(an_rpc)
          unless active_call.nil?
            @pool.schedule(active_call) do |ac|
              c, mth = ac
              begin
                rpc_descs[mth].run_server_method(c, rpc_handlers[mth])
              rescue StandardError => e
                c.send_status(code = GRPC::Core::StatusCodes::INTERNAL,
                              details = "Server handler failed")
              end
            end
          end
        rescue Core::CallError, RuntimeError => e
          # these might happen for various reasonse.  The correct behaviour of
          # the server is to log them and continue, if it's not shutting down.
          if running_state == :running
            GRPC.logger.warn("server call failed: #{e}")
          end
          next
        end
      end
      # @running_state should be :stopping here
      @run_mutex.synchronize { transition_running_state(:stopped) }
      GRPC.logger.info("stopped: #{self}")
    end

    def new_active_server_call(an_rpc)
      return nil if an_rpc.nil? || an_rpc.call.nil?

      # allow the metadata to be accessed from the call
      handle_call_tag = Object.new
      an_rpc.call.metadata = an_rpc.metadata  # attaches md to call for handlers
      GRPC.logger.debug("call md is #{an_rpc.metadata}")
      connect_md = nil
      unless @connect_md_proc.nil?
        connect_md = @connect_md_proc.call(an_rpc.method, an_rpc.metadata)
      end
      an_rpc.call.run_batch(@cq, handle_call_tag, INFINITE_FUTURE,
                            SEND_INITIAL_METADATA => connect_md)
      return nil unless available?(an_rpc)
      return nil unless implemented?(an_rpc)

      # Create the ActiveCall
      GRPC.logger.info("deadline is #{an_rpc.deadline}; (now=#{Time.now})")
      rpc_desc = rpc_descs[an_rpc.method.to_sym]
      c = ActiveCall.new(an_rpc.call, @cq,
                         rpc_desc.marshal_proc, rpc_desc.unmarshal_proc(:input),
                         an_rpc.deadline)
      mth = an_rpc.method.to_sym
      [c, mth]
    end

    protected

    def rpc_descs
      @rpc_descs ||= {}
    end

    def rpc_handlers
      @rpc_handlers ||= {}
    end

    def assert_valid_service_class(cls)
      unless cls.include?(GenericService)
        fail "#{cls} must 'include GenericService'"
      end
      if cls.rpc_descs.size.zero?
        fail "#{cls} should specify some rpc descriptions"
      end
      cls.assert_rpc_descs_have_methods
    end

    # This should be called while holding @run_mutex
    def add_rpc_descs_for(service)
      cls = service.is_a?(Class) ? service : service.class
      specs, handlers = (@rpc_descs ||= {}), (@rpc_handlers ||= {})
      cls.rpc_descs.each_pair do |name, spec|
        route = "/#{cls.service_name}/#{name}".to_sym
        fail "already registered: rpc #{route} from #{spec}" if specs.key? route
        specs[route] = spec
        rpc_name = GenericService.underscore(name.to_s).to_sym
        if service.is_a?(Class)
          handlers[route] = cls.new.method(rpc_name)
        else
          handlers[route] = service.method(rpc_name)
        end
        GRPC.logger.info("handling #{route} with #{handlers[route]}")
      end
    end
  end
end
