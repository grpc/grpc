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
require 'concurrent'

# GRPC contains the General RPC module.
module GRPC
  # RpcServer hosts a number of services and makes them available on the
  # network.
  class RpcServer
    include Core::CallOps
    include Core::TimeConsts
    extend ::Forwardable

    def_delegators :@server, :add_http2_port

    # Default poll period is 1s
    # Used for grpc server shutdown and thread pool shutdown timeouts
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
    # * pool_size: deprecated
    #
    # * max_waiting_requests: deprecated
    #
    # * poll_period: when present, the server polls for new events with this
    # period
    #
    # * connect_md_proc:
    # when non-nil is a proc for determining metadata to to send back the client
    # on receiving an invocation req.  The proc signature is:
    # {key: val, ..} func(method_name, {key: val, ...})
    #
    # * server_args:
    # A server arguments hash to be passed down to the underlying core server
    def initialize(pool_size:DEFAULT_MAX_POOL_SIZE,
                   min_pool_size:DEFAULT_MIN_POOL_SIZE,
                   max_waiting_requests:DEFAULT_MAX_WAITING_REQUESTS,
                   poll_period:DEFAULT_POLL_PERIOD,
                   connect_md_proc:nil,
                   server_args:{})
      @connect_md_proc = RpcServer.setup_connect_md_proc(connect_md_proc)
      @poll_period = poll_period

      # Bogus calculation below to use deprecated parameters to avoid
      # unused variable complaints from compiler
      @dummyvar = pool_size + min_pool_size + max_waiting_requests

      @pool = Concurrent::CachedThreadPool.new
      @run_cond = ConditionVariable.new
      @run_mutex = Mutex.new
      # running_state can take 4 values: :not_started, :running, :stopping, and
      # :stopped. State transitions can only proceed in that order.
      @running_state = :not_started
      @server = Core::Server.new(server_args)
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
      @server.close(deadline)
      @pool.shutdown
      @pool.wait_for_termination(@poll_period)
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
        @server.start
        transition_running_state(:running)
        @run_cond.broadcast
      end
      loop_handle_server_calls
    end

    alias_method :run_till_terminated, :run

    # Sends RESOURCE_EXHAUSTED if there are too many unprocessed jobs
    def available?(an_rpc)
      jobs_count, max = @pool.queue_length, @pool.max_queue
      GRPC.logger.info("waiting: #{jobs_count}, max: #{max}")

      # remaining capacity for ThreadPoolExecutors is -1 if unbounded
      return an_rpc if @pool.remaining_capacity != 0
      GRPC.logger.warn("NOT AVAILABLE: too many jobs_waiting: #{an_rpc}")
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
            @pool.post(active_call) do |ac|
              c, mth = ac
              begin
                rpc_descs[mth].run_server_method(c, rpc_handlers[mth])
              rescue StandardError
                c.send_status(GRPC::Core::StatusCodes::INTERNAL,
                              'Server handler failed')
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
