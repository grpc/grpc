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

require 'forwardable'
require_relative '../grpc'

# GRPC contains the General RPC module.
module GRPC
  # The BiDiCall class orchestrates exection of a BiDi stream on a client or
  # server.
  class BidiCall
    include Core::CallOps
    include Core::StatusCodes
    include Core::TimeConsts

    # Creates a BidiCall.
    #
    # BidiCall should only be created after a call is accepted.  That means
    # different things on a client and a server.  On the client, the call is
    # accepted after call.invoke. On the server, this is after call.accept.
    #
    # #initialize cannot determine if the call is accepted or not; so if a
    # call that's not accepted is used here, the error won't be visible until
    # the BidiCall#run is called.
    #
    # deadline is the absolute deadline for the call.
    #
    # @param call [Call] the call used by the ActiveCall
    # @param marshal [Function] f(obj)->string that marshal requests
    # @param unmarshal [Function] f(string)->obj that unmarshals responses
    # @param metadata_received [true|false] indicates if metadata has already
    #     been received. Should always be true for server calls
    def initialize(call, marshal, unmarshal, metadata_received: false,
                   req_view: nil, acall:)
      fail(ArgumentError, 'not a call') unless call.is_a? Core::Call
      @acall = acall
      @call = call
      @marshal = marshal
      @op_notifier = nil  # signals completion on clients
      @unmarshal = unmarshal
      @metadata_received = metadata_received
      @reads_complete = false
      @writes_complete = false
      @complete = false
      @done_mutex = Mutex.new
      @req_view = req_view
    end

    # Begins orchestration of the Bidi stream for a client sending requests.
    #
    # The method either returns an Enumerator of the responses, or accepts a
    # block that can be invoked with each response.
    #
    # @param requests the Enumerable of requests to send
    # @param set_input_stream_done [Proc] called back when we're done
    #   reading the input stream
    # @param set_output_stream_done [Proc] called back when we're done
    #   sending data on the output stream
    # @return an Enumerator of requests to yield
    def run_on_client(requests, set_output_stream_done)
      t = Thread.new do
        write_loop(requests, set_output_stream_done: set_output_stream_done)
      end

      if block_given?
        @acall.each_remote_read_then_finish { |v| yield v }
        t.join
      else
        Enumerator.new do |out|
          @acall.each_remote_read_then_finish { |v| out << v }
          t.join
        end
      end
    end

    # Begins orchestration of the Bidi stream for a server generating replies.
    #
    # N.B. gen_each_reply is a func(Enumerable<Requests>)
    #
    # It takes an enumerable of requests as an arg, in case there is a
    # relationship between the stream of requests and the stream of replies.
    #
    # This does not mean that must necessarily be one.  E.g, the replies
    # produced by gen_each_reply could ignore the received_msgs
    #
    # @param [Proc] gen_each_reply generates the BiDi stream replies.
    # @param [Enumerable] requests The enumerable of requests to run
    def run_on_server(gen_each_reply)
      requests = @acall.each_remote_read
      replies = nil

      # Pass in the optional call object parameter if possible
      if gen_each_reply.arity == 1
        replies = gen_each_reply.call(requests)
      elsif gen_each_reply.arity == 2
        replies = gen_each_reply.call(requests, @req_view)
      else
        fail 'Illegal arity of reply generator'
      end

      write_loop(replies, is_client: false)
    end

    private

    END_OF_READS = :end_of_reads
    END_OF_WRITES = :end_of_writes

    # set_output_stream_done is relevant on client-side
    def write_loop(requests, is_client: true, set_output_stream_done: nil)
      GRPC.logger.debug('bidi-write-loop: starting')
      count = 0
      requests.each do |req|
        GRPC.logger.debug("bidi-write-loop: #{count}")
        count += 1
        # Fails if status already received
        begin
          @acall.remote_send(req, false)
        rescue GRPC::Core::CallError => e
          # This is almost definitely caused by a status arriving while still
          # writing. Don't re-throw the error
          GRPC.logger.warn('bidi-write-loop: ended with error')
          GRPC.logger.warn(e)
          break
        end
      end
      GRPC.logger.debug("bidi-write-loop: #{count} writes done")
      if is_client
        GRPC.logger.debug("bidi-write-loop: client sent #{count}, waiting")
        begin
          @call.run_batch(SEND_CLOSE_FROM_CLIENT => nil)
        rescue GRPC::Core::CallError => e
          GRPC.logger.warn('bidi-write-loop: send close failed')
          GRPC.logger.warn(e)
        end
        GRPC.logger.debug('bidi-write-loop: done')
      end
      GRPC.logger.debug('bidi-write-loop: finished')
    rescue StandardError => e
      GRPC.logger.warn('bidi-write-loop: failed')
      GRPC.logger.warn(e)
      if is_client
        @call.cancel_with_status(GRPC::Core::StatusCodes::UNKNOWN,
                                 "GRPC bidi call error: #{e.inspect}")
      else
        raise e
      end
    ensure
      set_output_stream_done.call if is_client
    end
  end
end
