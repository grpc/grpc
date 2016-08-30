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
      proc { |o| o.class.method(marshal_method).call(o).to_s }
    end

    # @param [:input, :output] target determines whether to produce the an
    #                          unmarshal Proc for the rpc input parameter or
    #                          its output parameter
    #
    # @return [Proc] An unmarshal proc { |marshalled(instance)| instance }
    def unmarshal_proc(target)
      fail ArgumentError unless [:input, :output].include?(target)
      unmarshal_class = method(target).call
      unmarshal_class = unmarshal_class.type if unmarshal_class.is_a? Stream
      proc { |o| unmarshal_class.method(unmarshal_method).call(o) }
    end

    def run_server_method(active_call, mth)
      # While a server method is running, it might be cancelled, its deadline
      # might be reached, the handler could throw an unknown error, or a
      # well-behaved handler could throw a StatusError.
      if request_response?
        req = active_call.remote_read
        resp = mth.call(req, active_call.single_req_view)
        active_call.remote_send(resp)
      elsif client_streamer?
        resp = mth.call(active_call.multi_req_view)
        active_call.remote_send(resp)
      elsif server_streamer?
        req = active_call.remote_read
        replys = mth.call(req, active_call.single_req_view)
        replys.each { |r| active_call.remote_send(r) }
      else  # is a bidi_stream
        active_call.run_server_bidi(mth)
      end
      send_status(active_call, OK, 'OK', active_call.output_metadata)
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
      # This is raised when active_call#method.call exceeeds the deadline
      # event.  Send a status of deadline exceeded
      GRPC.logger.warn("late call: #{active_call}")
      send_status(active_call, DEADLINE_EXCEEDED, 'late')
    rescue StandardError => e
      # This will usuaally be an unhandled error in the handling code.
      # Send back a UNKNOWN status to the client
      GRPC.logger.warn("failed handler: #{active_call}; sending status:UNKNOWN")
      GRPC.logger.warn(e)
      send_status(active_call, UNKNOWN, 'no reason given')
    end

    def assert_arity_matches(mth)
      if request_response? || server_streamer?
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
