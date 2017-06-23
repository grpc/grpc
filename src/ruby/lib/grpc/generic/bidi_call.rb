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
                   req_view: nil)
      fail(ArgumentError, 'not a call') unless call.is_a? Core::Call
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
    # @param op_notifier a Notifier used to signal completion
    # @return an Enumerator of requests to yield
    def run_on_client(requests, op_notifier, &blk)
      @op_notifier = op_notifier
      @enq_th = Thread.new { write_loop(requests) }
      read_loop(&blk)
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
    # @param gen_each_reply [Proc] generates the BiDi stream replies.
    def run_on_server(gen_each_reply)
      # Pass in the optional call object parameter if possible
      if gen_each_reply.arity == 1
        replys = gen_each_reply.call(read_loop(is_client: false))
      elsif gen_each_reply.arity == 2
        replys = gen_each_reply.call(read_loop(is_client: false), @req_view)
      else
        fail 'Illegal arity of reply generator'
      end

      write_loop(replys, is_client: false)
    end

    private

    END_OF_READS = :end_of_reads
    END_OF_WRITES = :end_of_writes

    # signals that bidi operation is complete
    def notify_done
      return unless @op_notifier
      GRPC.logger.debug("bidi-notify-done: notifying  #{@op_notifier}")
      @op_notifier.notify(self)
    end

    # signals that a bidi operation is complete (read + write)
    def finished
      @done_mutex.synchronize do
        return unless @reads_complete && @writes_complete && !@complete
        @call.close
        @complete = true
      end
    end

    # performs a read using @call.run_batch, ensures metadata is set up
    def read_using_run_batch
      ops = { RECV_MESSAGE => nil }
      ops[RECV_INITIAL_METADATA] = nil unless @metadata_received
      batch_result = @call.run_batch(ops)
      unless @metadata_received
        @call.metadata = batch_result.metadata
        @metadata_received = true
      end
      batch_result
    end

    def write_loop(requests, is_client: true)
      GRPC.logger.debug('bidi-write-loop: starting')
      count = 0
      requests.each do |req|
        GRPC.logger.debug("bidi-write-loop: #{count}")
        count += 1
        payload = @marshal.call(req)
        # Fails if status already received
        begin
          @req_view.send_initial_metadata unless @req_view.nil?
          @call.run_batch(SEND_MESSAGE => payload)
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
        @call.run_batch(SEND_CLOSE_FROM_CLIENT => nil)
        GRPC.logger.debug('bidi-write-loop: done')
        notify_done
        @writes_complete = true
        finished
      end
      GRPC.logger.debug('bidi-write-loop: finished')
    rescue StandardError => e
      GRPC.logger.warn('bidi-write-loop: failed')
      GRPC.logger.warn(e)
      notify_done
      @writes_complete = true
      finished
      raise e
    end

    # Provides an enumerator that yields results of remote reads
    def read_loop(is_client: true)
      return enum_for(:read_loop,
                      is_client: is_client) unless block_given?
      GRPC.logger.debug('bidi-read-loop: starting')
      begin
        count = 0
        # queue the initial read before beginning the loop
        loop do
          GRPC.logger.debug("bidi-read-loop: #{count}")
          count += 1
          batch_result = read_using_run_batch

          # handle the next message
          if batch_result.message.nil?
            GRPC.logger.debug("bidi-read-loop: null batch #{batch_result}")

            if is_client
              batch_result = @call.run_batch(RECV_STATUS_ON_CLIENT => nil)
              @call.status = batch_result.status
              @call.trailing_metadata = @call.status.metadata if @call.status
              batch_result.check_status
              GRPC.logger.debug("bidi-read-loop: done status #{@call.status}")
            end

            GRPC.logger.debug('bidi-read-loop: done reading!')
            break
          end

          res = @unmarshal.call(batch_result.message)
          yield res
        end
      rescue StandardError => e
        GRPC.logger.warn('bidi: read-loop failed')
        GRPC.logger.warn(e)
        raise e
      end
      GRPC.logger.debug('bidi-read-loop: finished')
      @reads_complete = true
      finished
      # Make sure that the write loop is done done before finishing the call.
      # Note that blocking is ok at this point because we've already received
      # a status
      @enq_th.join if is_client
    end
  end
end
