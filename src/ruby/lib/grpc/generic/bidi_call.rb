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

require 'forwardable'
require 'grpc/grpc'

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
    # @param q [CompletionQueue] the completion queue used to accept
    #          the call
    # @param marshal [Function] f(obj)->string that marshal requests
    # @param unmarshal [Function] f(string)->obj that unmarshals responses
    # @param deadline [Fixnum] the deadline for the call to complete
    def initialize(call, q, marshal, unmarshal, deadline)
      fail(ArgumentError, 'not a call') unless call.is_a? Core::Call
      unless q.is_a? Core::CompletionQueue
        fail(ArgumentError, 'not a CompletionQueue')
      end
      @call = call
      @cq = q
      @deadline = deadline
      @marshal = marshal
      @readq = Queue.new
      @unmarshal = unmarshal
    end

    # Begins orchestration of the Bidi stream for a client sending requests.
    #
    # The method either returns an Enumerator of the responses, or accepts a
    # block that can be invoked with each response.
    #
    # @param requests the Enumerable of requests to send
    # @return an Enumerator of requests to yield
    def run_on_client(requests, &blk)
      @enq_th = start_write_loop(requests)
      @loop_th = start_read_loop
      replies = each_queued_msg
      return replies if blk.nil?
      replies.each { |r| blk.call(r) }
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
      replys = gen_each_reply.call(each_queued_msg)
      @enq_th = start_write_loop(replys, is_client: false)
      @loop_th = start_read_loop
    end

    private

    END_OF_READS = :end_of_reads
    END_OF_WRITES = :end_of_writes

    # each_queued_msg yields each message on this instances readq
    #
    # - messages are added to the readq by #read_loop
    # - iteration ends when the instance itself is added
    def each_queued_msg
      return enum_for(:each_queued_msg) unless block_given?
      count = 0
      loop do
        GRPC.logger.debug("each_queued_msg: msg##{count}")
        count += 1
        req = @readq.pop
        GRPC.logger.debug("each_queued_msg: req = #{req}")
        throw req if req.is_a? StandardError
        break if req.equal?(END_OF_READS)
        yield req
      end
      @enq_th.join if @enq_th.alive?
    end

    # during bidi-streaming, read the requests to send from a separate thread
    # read so that read_loop does not block waiting for requests to read.
    def start_write_loop(requests, is_client: true)
      Thread.new do  # TODO: run on a thread pool
        write_tag = Object.new
        begin
          count = 0
          requests.each do |req|
            GRPC.logger.debug("bidi-write_loop: #{count}")
            count += 1
            payload = @marshal.call(req)
            @call.run_batch(@cq, write_tag, INFINITE_FUTURE,
                            SEND_MESSAGE => payload)
          end
          if is_client
            GRPC.logger.debug("bidi-write-loop: sent #{count}, waiting")
            batch_result = @call.run_batch(@cq, write_tag, INFINITE_FUTURE,
                                           SEND_CLOSE_FROM_CLIENT => nil,
                                           RECV_STATUS_ON_CLIENT => nil)
            batch_result.check_status
          end
        rescue StandardError => e
          GRPC.logger.warn('bidi-write_loop: failed')
          GRPC.logger.warn(e)
          raise e
        end
      end
    end

    # starts the read loop
    def start_read_loop
      Thread.new do
        begin
          read_tag = Object.new
          count = 0

          # queue the initial read before beginning the loop
          loop do
            GRPC.logger.debug("bidi-read_loop: #{count}")
            count += 1
            # TODO: ensure metadata is read if available, currently it's not
            batch_result = @call.run_batch(@cq, read_tag, INFINITE_FUTURE,
                                           RECV_MESSAGE => nil)
            # handle the next message
            if batch_result.message.nil?
              @readq.push(END_OF_READS)
              GRPC.logger.debug('bidi-read-loop: done reading!')
              break
            end

            # push the latest read onto the queue and continue reading
            GRPC.logger.debug("received req: #{batch_result.message}")
            res = @unmarshal.call(batch_result.message)
            @readq.push(res)
          end

        rescue StandardError => e
          GRPC.logger.warn('bidi: read_loop failed')
          GRPC.logger.warn(e)
          @readq.push(e)  # let each_queued_msg terminate with this error
        end
      end
    end
  end
end
