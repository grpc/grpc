# Copyright 2014, Google Inc.
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
require 'grpc'

module GRPC

  # The BiDiCall class orchestrates exection of a BiDi stream on a client or
  # server.
  class BidiCall
    include Core::CompletionType
    include Core::StatusCodes
    include Core::TimeConsts

    # Creates a BidiCall.
    #
    # BidiCall should only be created after a call is accepted.  That means
    # different things on a client and a server.  On the client, the call is
    # accepted after call.start_invoke followed by receipt of the corresponding
    # INVOKE_ACCEPTED.  On the server, this is after call.accept.
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
    # @param finished_tag [Object] the object used as the call's finish tag,
    def initialize(call, q, marshal, unmarshal, deadline, finished_tag)
      raise ArgumentError.new('not a call') unless call.is_a?Core::Call
      if !q.is_a?Core::CompletionQueue
        raise ArgumentError.new('not a CompletionQueue')
      end
      @call = call
      @cq = q
      @deadline = deadline
      @finished_tag = finished_tag
      @marshal = marshal
      @readq = Queue.new
      @unmarshal = unmarshal
      @writeq = Queue.new
    end

    # Begins orchestration of the Bidi stream for a client sending requests.
    #
    # The method either returns an Enumerator of the responses, or accepts a
    # block that can be invoked with each response.
    #
    # @param requests the Enumerable of requests to send
    # @return an Enumerator of requests to yield
    def run_on_client(requests, &blk)
      enq_th = enqueue_for_sending(requests)
      loop_th = start_read_write_loop
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
      enq_th = enqueue_for_sending(replys)
      loop_th = start_read_write_loop(is_client:false)
      loop_th.join
      enq_th.join
    end

    private

    END_OF_READS = :end_of_reads
    END_OF_WRITES = :end_of_writes

    # each_queued_msg yields each message on this instances readq
    #
    # - messages are added to the readq by #read_write_loop
    # - iteration ends when the instance itself is added
    def each_queued_msg
      return enum_for(:each_queued_msg) if !block_given?
      count = 0
      loop do
        logger.debug("each_queued_msg: msg##{count}")
        count += 1
        req = @readq.pop
        throw req if req.is_a?StandardError
        break if req.equal?(END_OF_READS)
        yield req
      end
    end

    # during bidi-streaming, read the requests to send from a separate thread
    # read so that read_write_loop does not block waiting for requests to read.
    def enqueue_for_sending(requests)
      Thread.new do  # TODO(temiola) run on a thread pool
        begin
          requests.each { |req| @writeq.push(req)}
          @writeq.push(END_OF_WRITES)
        rescue StandardError => e
          logger.warn('enqueue_for_sending failed')
          logger.warn(e)
          @writeq.push(e)
        end
      end
    end

    # starts the read_write loop
    def start_read_write_loop(is_client: true)
      t = Thread.new do
        begin
          read_write_loop(is_client: is_client)
        rescue StandardError => e
          logger.warn('start_read_write_loop failed')
          logger.warn(e)
          @readq.push(e)  # let each_queued_msg terminate with the error
        end
      end
      t.priority = 3  # hint that read_write_loop threads should be favoured
      t
    end

    # drain_writeq removes any outstanding message on the writeq
    def drain_writeq
      while @writeq.size != 0 do
        discarded = @writeq.pop
        logger.warn("discarding: queued write: #{discarded}")
      end
    end

    # sends the next queued write
    #
    # The return value is an array with three values
    # - the first indicates if a writes was started
    # - the second that all writes are done
    # - the third indicates that are still writes to perform but they are lates
    #
    # If value pulled from writeq is a StandardError, the producer hit an error
    # that should be raised.
    #
    # @param is_client [Boolean] when true, writes_done will be called when the
    # last entry is read from the writeq
    #
    # @return [in_write, done_writing]
    def next_queued_write(is_client: true)
      in_write, done_writing = false, false

      # send the next item on the queue if there is any
      return [in_write, done_writing] if @writeq.size == 0

      # TODO(temiola): provide a queue class that returns nil after a timeout
      req = @writeq.pop
      if req.equal?(END_OF_WRITES)
        logger.debug('done writing after last req')
        if is_client
          logger.debug('sent writes_done after last req')
          @call.writes_done(self)
        end
        done_writing = true
        return [in_write, done_writing]
      elsif req.is_a?(StandardError)  # used to signal an error in the producer
        logger.debug('done writing due to a failure')
        if is_client
          logger.debug('sent writes_done after a failure')
          @call.writes_done(self)
        end
        logger.warn(req)
        done_writing = true
        return [in_write, done_writing]
      end

      # send the payload
      payload = @marshal.call(req)
      @call.start_write(Core::ByteBuffer.new(payload), self)
      logger.debug("rwloop: sent payload #{req.inspect}")
      in_write = true
      return [in_write, done_writing]
    end

    # read_write_loop takes items off the write_queue and sends them, reads
    # msgs and adds them to the read queue.
    def read_write_loop(is_client: true)
      done_reading, done_writing = false, false
      finished, pre_finished = false, false
      in_write, writes_late = false, false
      count = 0

      # queue the initial read before beginning the loop
      @call.start_read(self)

      loop do
        # whether or not there are outstanding writes is independent of the
        # next event from the completion queue.  The producer may queue the
        # first msg at any time, e.g, after the loop is started running. So,
        # it's essential for the loop to check for possible writes here, in
        # order to correctly begin writing.
        if !in_write and !done_writing
          in_write, done_writing = next_queued_write(is_client: is_client)
        end
        logger.debug("rwloop is_client? #{is_client}")
        logger.debug("rwloop count: #{count}")
        count += 1

        # Loop control:
        #
        # - Break when no further events need to read. On clients, this means
        # waiting for a FINISHED, servers just need to wait for all reads and
        # writes to be done.
        #
        # - Also, don't read an event unless there's one expected.  This can
        # happen, e.g, when all the reads are done, there are no writes
        # available, but writing is not complete.
        logger.debug("done_reading? #{done_reading}")
        logger.debug("done_writing? #{done_writing}")
        logger.debug("finish accepted? #{pre_finished}")
        logger.debug("finished? #{finished}")
        logger.debug("in write? #{in_write}")
        if is_client
          break if done_writing and done_reading and pre_finished and finished
          logger.debug('waiting for another event')
          if in_write or !done_reading or !pre_finished
            logger.debug('waiting for another event')
            ev = @cq.pluck(self, INFINITE_FUTURE)
          elsif !finished
            logger.debug('waiting for another event')
            ev = @cq.pluck(@finished_tag, INFINITE_FUTURE)
          else
            next  # no events to wait on, but not done writing
          end
        else
          break if done_writing and done_reading
          if in_write or !done_reading
            logger.debug('waiting for another event')
            ev = @cq.pluck(self, INFINITE_FUTURE)
          else
            next  # no events to wait on, but not done writing
          end
        end

        # handle the next event.
        if ev.nil?
          drain_writeq
          raise OutOfTime
        elsif ev.type == WRITE_ACCEPTED
          logger.debug('write accepted!')
          in_write = false
          next
        elsif ev.type == FINISH_ACCEPTED
          logger.debug('finish accepted!')
          pre_finished = true
          next
        elsif ev.type == READ
          logger.debug("received req: #{ev.result.inspect}")
          if ev.result.nil?
            logger.debug('done reading!')
            done_reading = true
            @readq.push(END_OF_READS)
          else
            # push the latest read onto the queue and continue reading
            logger.debug("received req.to_s: #{ev.result.to_s}")
            res = @unmarshal.call(ev.result.to_s)
            logger.debug("req (unmarshalled): #{res.inspect}")
            @readq.push(res)
            if !done_reading
              @call.start_read(self)
            end
          end
        elsif ev.type == FINISHED
          logger.debug("finished! with status:#{ev.result.inspect}")
          finished = true
          ev.call.status = ev.result
          if ev.result.code != OK
            raise BadStatus.new(ev.result.code, ev.result.details)
          end
        end
      end
    end

  end

end
