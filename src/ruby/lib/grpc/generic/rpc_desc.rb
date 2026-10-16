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

# GRPC contains the General RPC module.
module GRPC
  # RpcDesc is a Descriptor of an RPC method.
  class RpcDesc < Struct.new(:name, :input, :output, :marshal_method,
                             :unmarshal_method)
    include Core::StatusCodes

    # Used to wrap a message class to indicate that it needs to be streamed.
    class Stream
      attr_accessor :type

      def initialize(type)
        @type = type
      end
    end

    # @return [Proc] { |instance| marshalled(instance) }
    def marshal_proc
      proc { |o| o.class.send(marshal_method, o).to_s }
    end

    # @param [:input, :output] target determines whether to produce the an
    #                          unmarshal Proc for the rpc input parameter or
    #                          its output parameter
    #
    # @return [Proc] An unmarshal proc { |marshalled(instance)| instance }
    def unmarshal_proc(target)
      fail ArgumentError unless [:input, :output].include?(target)
      unmarshal_class = send(target)
      unmarshal_class = unmarshal_class.type if unmarshal_class.is_a? Stream
      proc { |o| unmarshal_class.send(unmarshal_method, o) }
    end

    def handle_request_response(active_call, mth, inter_ctx)
      req = active_call.read_unary_request
      call = active_call.single_req_view

      inter_ctx.intercept!(
        :request_response,
        method: mth,
        call: call,
        request: req
      ) do
        resp = mth.call(req, call)
        active_call.server_unary_response(
          resp,
          trailing_metadata: active_call.output_metadata
        )
      end
    end

    def handle_client_streamer(active_call, mth, inter_ctx)
      call = active_call.multi_req_view

      inter_ctx.intercept!(
        :client_streamer,
        method: mth,
        call: call
      ) do
        resp = mth.call(call)
        active_call.server_unary_response(
          resp,
          trailing_metadata: active_call.output_metadata
        )
      end
    end

    def handle_server_streamer(active_call, mth, inter_ctx)
      req = active_call.read_unary_request
      call = active_call.single_req_view

      inter_ctx.intercept!(
        :server_streamer,
        method: mth,
        call: call,
        request: req
      ) do
        replies = mth.call(req, call)
        replies.each { |r| active_call.remote_send(r) }
        send_status(active_call, OK, 'OK', active_call.output_metadata)
      end
    end

    ##
    # @param [GRPC::ActiveCall] active_call
    # @param [Method] mth
    # @param [Array<GRPC::InterceptionContext>] inter_ctx
    #
    def handle_bidi_streamer(active_call, mth, inter_ctx)
      active_call.run_server_bidi(mth, inter_ctx)
      send_status(active_call, OK, 'OK', active_call.output_metadata)
    end

    ##
    # @param [GRPC::ActiveCall] active_call The current active call object
    #   for the request
    # @param [Method] mth The current RPC method being called
    # @param [GRPC::InterceptionContext] inter_ctx The interception context
    #   being executed
    #
    def run_server_method(active_call, mth, inter_ctx = InterceptionContext.new)
      # While a server method is running, it might be cancelled, its deadline
      # might be reached, the handler could throw an unknown error, or a
      # well-behaved handler could throw a StatusError.
      if request_response?
        handle_request_response(active_call, mth, inter_ctx)
      elsif client_streamer?
        handle_client_streamer(active_call, mth, inter_ctx)
      elsif server_streamer?
        handle_server_streamer(active_call, mth, inter_ctx)
      else  # is a bidi_stream
        handle_bidi_streamer(active_call, mth, inter_ctx)
      end
    rescue BadStatus => e
      # this is raised by handlers that want GRPC to send an application error
      # code and detail message and some additional app-specific metadata.
      GRPC.logger.debug("app err:#{active_call}, status:#{e.code}:#{e.details}")
      send_status(active_call, e.code, e.details, e.metadata)
    rescue Core::CallError => e
      # This is raised by GRPC internals but should rarely, if ever happen.
      # Log it, but don't notify the other endpoint..
      GRPC.logger.warn("failed call: #{active_call}\n#{e}")
    rescue Core::OutOfTime
      # This is raised when active_call#method.call exceeds the deadline
      # event.  Send a status of deadline exceeded
      GRPC.logger.warn("late call: #{active_call}")
      send_status(active_call, DEADLINE_EXCEEDED, 'late')
    rescue StandardError, NotImplementedError => e
      # This will usuaally be an unhandled error in the handling code.
      # Send back a UNKNOWN status to the client
      #
      # Note: this intentionally does not map NotImplementedError to
      # UNIMPLEMENTED because NotImplementedError is intended for low-level
      # OS interaction (e.g. syscalls) not supported by the current OS.
      GRPC.logger.warn("failed handler: #{active_call}; sending status:UNKNOWN")
      GRPC.logger.warn(e)
      send_status(active_call, UNKNOWN, "#{e.class}: #{e.message}")
    end

    def assert_arity_matches(mth)
      # A bidi handler function can optionally be passed a second
      # call object parameter for access to metadata, cancelling, etc.
      if bidi_streamer?
        if mth.arity != 2 && mth.arity != 1
          fail arity_error(mth, 2, "should be #{mth.name}(req, call) or " \
            "#{mth.name}(req)")
        end
      elsif request_response? || server_streamer?
        if mth.arity != 2
          fail arity_error(mth, 2, "should be #{mth.name}(req, call)")
        end
      else
        if mth.arity != 1
          fail arity_error(mth, 1, "should be #{mth.name}(call)")
        end
      end
    end

    def request_response?
      !input.is_a?(Stream) && !output.is_a?(Stream)
    end

    def client_streamer?
      input.is_a?(Stream) && !output.is_a?(Stream)
    end

    def server_streamer?
      !input.is_a?(Stream) && output.is_a?(Stream)
    end

    def bidi_streamer?
      input.is_a?(Stream) && output.is_a?(Stream)
    end

    def arity_error(mth, want, msg)
      "##{mth.name}: bad arg count; got:#{mth.arity}, want:#{want}, #{msg}"
    end

    def send_status(active_client, code, details, metadata = {})
      details = 'Not sure why' if details.nil?
      GRPC.logger.debug("Sending status  #{code}:#{details}")
      active_client.send_status(code, details, code == OK, metadata: metadata)
    rescue StandardError => e
      GRPC.logger.warn("Could not send status #{code}:#{details}")
      GRPC.logger.warn(e)
    end
  end
end
