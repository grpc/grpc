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

require_relative '../grpc'
require_relative 'active_call'
require_relative 'service'
require 'thread'

# GRPC contains the General RPC module.
module GRPC
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

      # Each worker thread has its own queue to push and pull jobs
      # these queues are put into @ready_queues when that worker is idle
      @ready_workers = Queue.new
    end

    # Returns the number of jobs waiting
    def jobs_waiting
      @jobs.size
    end

    def ready_for_work?
      # Busy worker threads are either doing work, or have a single job
      # waiting on them. Workers that are idle with no jobs waiting
      # have their "queues" in @ready_workers
      !@ready_workers.empty?
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
        fail 'No worker threads available' if @ready_workers.empty?
        worker_queue = @ready_workers.pop

        fail 'worker already has a task waiting' unless worker_queue.empty?
        worker_queue << [blk, args]
      end
    end

    # Starts running the jobs in the thread pool.
    def start
      @stop_mutex.synchronize do
        fail 'already stopped' if @stopped
      end
      until @workers.size == @size.to_i
        new_worker_queue = Queue.new
        @ready_workers << new_worker_queue
        next_thread = Thread.new(new_worker_queue) do |jobs|
          catch(:exit) do  # allows { throw :exit } to kill a thread
            loop_execute_jobs(jobs)
          end
          remove_current_thread
        end
        @workers << next_thread
      end
    end

    # Stops the jobs in the pool
    def stop
      GRPC.logger.info('stopping, will wait for all the workers to exit')
      @stop_mutex.synchronize do  # wait @keep_alive seconds for workers to stop
        @stopped = true
        loop do
          break unless ready_for_work?
          worker_queue = @ready_workers.pop
          worker_queue << [proc { throw :exit }, []]
        end
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

    def loop_execute_jobs(worker_queue)
      loop do
        begin
          blk, args = worker_queue.pop
          blk.call(*args)
        rescue StandardError => e
          GRPC.logger.warn('Error in worker thread')
          GRPC.logger.warn(e)
        end
        # there shouldn't be any work given to this thread while its busy
        fail('received a task while busy') unless worker_queue.empty?
        @stop_mutex.synchronize do
          return if @stopped
          @ready_workers << worker_queue
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

    # Default thread pool size is 30
    DEFAULT_POOL_SIZE = 30

    # Deprecated due to internal changes to the thread pool
    DEFAULT_MAX_WAITING_REQUESTS = 20

    # Default poll period is 1s
    DEFAULT_POLL_PERIOD = 1

    # Signal check period is 0.25s
    SIGNAL_CHECK_PERIOD = 0.25

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
    # instance.
    #
    # * pool_size: the size of the thread pool the server uses to run its
    # threads. No more concurrent requests can be made than the size
    # of the thread pool
    #
    # * max_waiting_requests: Deprecated due to internal changes to the thread
    # pool. This is still an argument for compatibility but is ignored.
    #
    # * poll_period: The amount of time in seconds to wait for
    # currently-serviced RPC's to finish before cancelling them when shutting
    # down the server.
    #
    # * pool_keep_alive: The amount of time in seconds to wait
    # for currently busy thread-pool threads to finish before
    # forcing an abrupt exit to each thread.
    #
    # * connect_md_proc:
    # when non-nil is a proc for determining metadata to to send back the client
    # on receiving an invocation req.  The proc signature is:
    #   {key: val, ..} func(method_name, {key: val, ...})
    #
    # * server_args:
    # A server arguments hash to be passed down to the underlying core server
    #
    # * interceptors:
    # Am array of GRPC::ServerInterceptor objects that will be used for
    # intercepting server handlers to provide extra functionality.
    # Interceptors are an EXPERIMENTAL API.
    #
    def initialize(pool_size: DEFAULT_POOL_SIZE,
                   max_waiting_requests: DEFAULT_MAX_WAITING_REQUESTS,
                   poll_period: DEFAULT_POLL_PERIOD,
                   pool_keep_alive: GRPC::RpcServer::DEFAULT_POOL_SIZE,
                   connect_md_proc: nil,
                   server_args: {},
                   interceptors: [])
      @connect_md_proc = RpcServer.setup_connect_md_proc(connect_md_proc)
      @max_waiting_requests = max_waiting_requests
      @poll_period = poll_period
      @pool_size = pool_size
      @pool = Pool.new(@pool_size, keep_alive: pool_keep_alive)
      @run_cond = ConditionVariable.new
      @run_mutex = Mutex.new
      # running_state can take 4 values: :not_started, :running, :stopping, and
      # :stopped. State transitions can only proceed in that order.
      @running_state = :not_started
      @server = Core::Server.new(server_args)
      @interceptors = InterceptorRegistry.new(interceptors)
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
        deadline = from_relative_time(@poll_period)
        @server.shutdown_and_notify(deadline)
      end
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
    # @return [true, false] true if the server is running, false otherwise
    def wait_till_running(timeout = nil)
      @run_mutex.synchronize do
        @run_cond.wait(@run_mutex, timeout) if @running_state == :not_started
        return @running_state == :running
      end
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

    alias_method :run_till_terminated, :run

    # Sends RESOURCE_EXHAUSTED if there are too many unprocessed jobs
    def available?(an_rpc)
      return an_rpc if @pool.ready_for_work?
      GRPC.logger.warn('no free worker threads currently')
      noop = proc { |x| x }

      # Create a new active call that knows that metadata hasn't been
      # sent yet
      c = ActiveCall.new(an_rpc.call, noop, noop, an_rpc.deadline,
                         metadata_received: true, started: false)
      c.send_status(GRPC::Core::StatusCodes::RESOURCE_EXHAUSTED, '')
      nil
    end

    # Sends UNIMPLEMENTED if the method is not implemented by this server
    def implemented?(an_rpc)
      mth = an_rpc.method.to_sym
      return an_rpc if rpc_descs.key?(mth)
      GRPC.logger.warn("UNIMPLEMENTED: #{an_rpc}")
      noop = proc { |x| x }

      # Create a new active call that knows that
      # metadata hasn't been sent yet
      c = ActiveCall.new(an_rpc.call, noop, noop, an_rpc.deadline,
                         metadata_received: true, started: false)
      c.send_status(GRPC::Core::StatusCodes::UNIMPLEMENTED, '')
      nil
    end

    # handles calls to the server
    def loop_handle_server_calls
      fail 'not started' if running_state == :not_started
      while running_state == :running
        begin
          an_rpc = @server.request_call
          break if (!an_rpc.nil?) && an_rpc.call.nil?
          active_call = new_active_server_call(an_rpc)
          unless active_call.nil?
            @pool.schedule(active_call) do |ac|
              c, mth = ac
              begin
                rpc_descs[mth].run_server_method(
                  c,
                  rpc_handlers[mth],
                  @interceptors.build_context
                )
              rescue StandardError
                c.send_status(GRPC::Core::StatusCodes::INTERNAL,
                              'Server handler failed')
              end
            end
          end
        rescue Core::CallError, RuntimeError => e
          # these might happen for various reasons.  The correct behavior of
          # the server is to log them and continue, if it's not shutting down.
          if running_state == :running
            GRPC.logger.warn("server call failed: #{e}")
          end
          next
        end
      end
      # @running_state should be :stopping here
      @run_mutex.synchronize do
        transition_running_state(:stopped)
        GRPC.logger.info("stopped: #{self}")
        @server.close
      end
    end

    def new_active_server_call(an_rpc)
      return nil if an_rpc.nil? || an_rpc.call.nil?

      # allow the metadata to be accessed from the call
      an_rpc.call.metadata = an_rpc.metadata  # attaches md to call for handlers
      connect_md = nil
      unless @connect_md_proc.nil?
        connect_md = @connect_md_proc.call(an_rpc.method, an_rpc.metadata)
      end

      return nil unless available?(an_rpc)
      return nil unless implemented?(an_rpc)

      # Create the ActiveCall. Indicate that metadata hasnt been sent yet.
      GRPC.logger.info("deadline is #{an_rpc.deadline}; (now=#{Time.now})")
      rpc_desc = rpc_descs[an_rpc.method.to_sym]
      c = ActiveCall.new(an_rpc.call,
                         rpc_desc.marshal_proc,
                         rpc_desc.unmarshal_proc(:input),
                         an_rpc.deadline,
                         metadata_received: true,
                         started: false,
                         metadata_to_send: connect_md)
      c.attach_peer_cert(an_rpc.call.peer_cert)
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
      fail "#{cls} should specify some rpc descriptions" if
        cls.rpc_descs.size.zero?
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
