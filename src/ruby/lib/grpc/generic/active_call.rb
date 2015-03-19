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
require 'grpc/generic/bidi_call'

def assert_event_type(ev, want)
  fail OutOfTime if ev.nil?
  got = ev.type
  fail "Unexpected rpc event: got #{got}, want #{want}" unless got == want
end

# GRPC contains the General RPC module.
module GRPC
  # The ActiveCall class provides simple methods for sending marshallable
  # data to a call
  class ActiveCall
    include Core::CompletionType
    include Core::StatusCodes
    include Core::TimeConsts
    attr_reader(:deadline)

    # client_invoke begins a client invocation.
    #
    # Flow Control note: this blocks until flow control accepts that client
    # request can go ahead.
    #
    # deadline is the absolute deadline for the call.
    #
    # == Keyword Arguments ==
    # any keyword arguments are treated as metadata to be sent to the server
    # if a keyword value is a list, multiple metadata for it's key are sent
    #
    # @param call [Call] a call on which to start and invocation
    # @param q [CompletionQueue] the completion queue
    # @param deadline [Fixnum,TimeSpec] the deadline
    def self.client_invoke(call, q, _deadline, **kw)
      fail(ArgumentError, 'not a call') unless call.is_a? Core::Call
      unless q.is_a? Core::CompletionQueue
        fail(ArgumentError, 'not a CompletionQueue')
      end
      call.add_metadata(kw) if kw.length > 0
      client_metadata_read = Object.new
      finished_tag = Object.new
      call.invoke(q, client_metadata_read, finished_tag)
      [finished_tag, client_metadata_read]
    end

    # Creates an ActiveCall.
    #
    # ActiveCall should only be created after a call is accepted.  That
    # means different things on a client and a server.  On the client, the
    # call is accepted after calling call.invoke.  On the server, this is
    # after call.accept.
    #
    # #initialize cannot determine if the call is accepted or not; so if a
    # call that's not accepted is used here, the error won't be visible until
    # the ActiveCall methods are called.
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
    #                              if the call has begun
    # @param read_metadata_tag [Object] the object used as the call's finish
    #                                   tag, if the call has begun
    # @param started [true|false] indicates if the call has begun
    def initialize(call, q, marshal, unmarshal, deadline, finished_tag: nil,
                   read_metadata_tag: nil, started: true)
      fail(ArgumentError, 'not a call') unless call.is_a? Core::Call
      unless q.is_a? Core::CompletionQueue
        fail(ArgumentError, 'not a CompletionQueue')
      end
      @call = call
      @cq = q
      @deadline = deadline
      @finished_tag = finished_tag
      @read_metadata_tag = read_metadata_tag
      @marshal = marshal
      @started = started
      @unmarshal = unmarshal
    end

    # Obtains the status of the call.
    #
    # this value is nil until the call completes
    # @return this call's status
    def status
      @call.status
    end

    # Obtains the metadata of the call.
    #
    # At the start of the call this will be nil.  During the call this gets
    # some values as soon as the other end of the connection acknowledges the
    # request.
    #
    # @return this calls's metadata
    def metadata
      @call.metadata
    end

    # Cancels the call.
    #
    # Cancels the call.  The call does not return any result, but once this it
    # has been called, the call should eventually terminate.  Due to potential
    # races between the execution of the cancel and the in-flight request, the
    # result of the call after calling #cancel is indeterminate:
    #
    # - the call may terminate with a BadStatus exception, with code=CANCELLED
    # - the call may terminate with OK Status, and return a response
    # - the call may terminate with a different BadStatus exception if that
    #   was happening
    def cancel
      @call.cancel
    end

    # indicates if the call is shutdown
    def shutdown
      @shutdown ||= false
    end

    # indicates if the call is cancelled.
    def cancelled
      @cancelled ||= false
    end

    # multi_req_view provides a restricted view of this ActiveCall for use
    # in a server client-streaming handler.
    def multi_req_view
      MultiReqView.new(self)
    end

    # single_req_view provides a restricted view of this ActiveCall for use in
    # a server request-response handler.
    def single_req_view
      SingleReqView.new(self)
    end

    # operation provides a restricted view of this ActiveCall for use as
    # a Operation.
    def operation
      Operation.new(self)
    end

    # writes_done indicates that all writes are completed.
    #
    # It blocks until the remote endpoint acknowledges by sending a FINISHED
    # event, unless assert_finished is set to false.  Any calls to
    # #remote_send after this call will fail.
    #
    # @param assert_finished [true, false] when true(default), waits for
    # FINISHED.
    def writes_done(assert_finished = true)
      @call.writes_done(self)
      ev = @cq.pluck(self, INFINITE_FUTURE)
      begin
        assert_event_type(ev, FINISH_ACCEPTED)
        logger.debug("Writes done: waiting for finish? #{assert_finished}")
      ensure
        ev.close
      end

      return unless assert_finished
      ev = @cq.pluck(@finished_tag, INFINITE_FUTURE)
      fail 'unexpected nil event' if ev.nil?
      ev.close
      @call.status
    end

    # finished waits until the call is completed.
    #
    # It blocks until the remote endpoint acknowledges by sending a FINISHED
    # event.
    def finished
      ev = @cq.pluck(@finished_tag, INFINITE_FUTURE)
      begin
        fail "unexpected event: #{ev.inspect}" unless ev.type == FINISHED
        if @call.metadata.nil?
          @call.metadata = ev.result.metadata
        else
          @call.metadata.merge!(ev.result.metadata)
        end

        if ev.result.code != Core::StatusCodes::OK
          fail BadStatus.new(ev.result.code, ev.result.details)
        end
        res = ev.result
      ensure
        ev.close
      end
      res
    end

    # remote_send sends a request to the remote endpoint.
    #
    # It blocks until the remote endpoint acknowledges by sending a
    # WRITE_ACCEPTED.  req can be marshalled already.
    #
    # @param req [Object, String] the object to send or it's marshal form.
    # @param marshalled [false, true] indicates if the object is already
    # marshalled.
    def remote_send(req, marshalled = false)
      assert_queue_is_ready
      logger.debug("sending #{req.inspect}, marshalled? #{marshalled}")
      if marshalled
        payload = req
      else
        payload = @marshal.call(req)
      end
      @call.start_write(Core::ByteBuffer.new(payload), self)

      # call queue#pluck, and wait for WRITE_ACCEPTED, so as not to return
      # until the flow control allows another send on this call.
      ev = @cq.pluck(self, INFINITE_FUTURE)
      begin
        assert_event_type(ev, WRITE_ACCEPTED)
      ensure
        ev.close
      end
    end

    # send_status sends a status to the remote endpoint
    #
    # @param code [int] the status code to send
    # @param details [String] details
    # @param assert_finished [true, false] when true(default), waits for
    # FINISHED.
    def send_status(code = OK, details = '', assert_finished = false)
      assert_queue_is_ready
      @call.start_write_status(code, details, self)
      ev = @cq.pluck(self, INFINITE_FUTURE)
      begin
        assert_event_type(ev, FINISH_ACCEPTED)
      ensure
        ev.close
      end
      logger.debug("Status sent: #{code}:'#{details}'")
      return finished if assert_finished
      nil
    end

    # remote_read reads a response from the remote endpoint.
    #
    # It blocks until the remote endpoint sends a READ or FINISHED event.  On
    # a READ, it returns the response after unmarshalling it. On
    # FINISHED, it returns nil if the status is OK, otherwise raising
    # BadStatus
    def remote_read
      if @call.metadata.nil? && !@read_metadata_tag.nil?
        ev = @cq.pluck(@read_metadata_tag, INFINITE_FUTURE)
        assert_event_type(ev, CLIENT_METADATA_READ)
        @call.metadata = ev.result
        @read_metadata_tag = nil
      end

      @call.start_read(self)
      ev = @cq.pluck(self, INFINITE_FUTURE)
      begin
        assert_event_type(ev, READ)
        logger.debug("received req: #{ev.result.inspect}")
        unless ev.result.nil?
          logger.debug("received req.to_s: #{ev.result}")
          res = @unmarshal.call(ev.result.to_s)
          logger.debug("received_req (unmarshalled): #{res.inspect}")
          return res
        end
      ensure
        ev.close
      end
      logger.debug('found nil; the final response has been sent')
      nil
    end

    # each_remote_read passes each response to the given block or returns an
    # enumerator the responses if no block is given.
    #
    # == Enumerator ==
    #
    # * #next blocks until the remote endpoint sends a READ or FINISHED
    # * for each read, enumerator#next yields the response
    # * on status
    #    * if it's is OK, enumerator#next raises StopException
    #    * if is not OK, enumerator#next raises RuntimeException
    #
    # == Block ==
    #
    # * if provided it is executed for each response
    # * the call blocks until no more responses are provided
    #
    # @return [Enumerator] if no block was given
    def each_remote_read
      return enum_for(:each_remote_read) unless block_given?
      loop do
        resp = remote_read
        break if resp.is_a? Struct::Status  # is an OK status
        break if resp.nil?  # the last response was received
        yield resp
      end
    end

    # each_remote_read_then_finish passes each response to the given block or
    # returns an enumerator of the responses if no block is given.
    #
    # It is like each_remote_read, but it blocks on finishing on detecting
    # the final message.
    #
    # == Enumerator ==
    #
    # * #next blocks until the remote endpoint sends a READ or FINISHED
    # * for each read, enumerator#next yields the response
    # * on status
    #    * if it's is OK, enumerator#next raises StopException
    #    * if is not OK, enumerator#next raises RuntimeException
    #
    # == Block ==
    #
    # * if provided it is executed for each response
    # * the call blocks until no more responses are provided
    #
    # @return [Enumerator] if no block was given
    def each_remote_read_then_finish
      return enum_for(:each_remote_read_then_finish) unless block_given?
      loop do
        resp = remote_read
        break if resp.is_a? Struct::Status  # is an OK status
        if resp.nil?  # the last response was received, but not finished yet
          finished
          break
        end
        yield resp
      end
    end

    # request_response sends a request to a GRPC server, and returns the
    # response.
    #
    # == Keyword Arguments ==
    # any keyword arguments are treated as metadata to be sent to the server
    # if a keyword value is a list, multiple metadata for it's key are sent
    #
    # @param req [Object] the request sent to the server
    # @return [Object] the response received from the server
    def request_response(req, **kw)
      start_call(**kw) unless @started
      remote_send(req)
      writes_done(false)
      response = remote_read
      finished unless response.is_a? Struct::Status
      response
    end

    # client_streamer sends a stream of requests to a GRPC server, and
    # returns a single response.
    #
    # requests provides an 'iterable' of Requests. I.e. it follows Ruby's
    # #each enumeration protocol. In the simplest case, requests will be an
    # array of marshallable objects; in typical case it will be an Enumerable
    # that allows dynamic construction of the marshallable objects.
    #
    # == Keyword Arguments ==
    # any keyword arguments are treated as metadata to be sent to the server
    # if a keyword value is a list, multiple metadata for it's key are sent
    #
    # @param requests [Object] an Enumerable of requests to send
    # @return [Object] the response received from the server
    def client_streamer(requests, **kw)
      start_call(**kw) unless @started
      requests.each { |r| remote_send(r) }
      writes_done(false)
      response = remote_read
      finished unless response.is_a? Struct::Status
      response
    end

    # server_streamer sends one request to the GRPC server, which yields a
    # stream of responses.
    #
    # responses provides an enumerator over the streamed responses, i.e. it
    # follows Ruby's #each iteration protocol.  The enumerator blocks while
    # waiting for each response, stops when the server signals that no
    # further responses will be supplied.  If the implicit block is provided,
    # it is executed with each response as the argument and no result is
    # returned.
    #
    # == Keyword Arguments ==
    # any keyword arguments are treated as metadata to be sent to the server
    # if a keyword value is a list, multiple metadata for it's key are sent
    # any keyword arguments are treated as metadata to be sent to the server.
    #
    # @param req [Object] the request sent to the server
    # @return [Enumerator|nil] a response Enumerator
    def server_streamer(req, **kw)
      start_call(**kw) unless @started
      remote_send(req)
      writes_done(false)
      replies = enum_for(:each_remote_read_then_finish)
      return replies unless block_given?
      replies.each { |r| yield r }
    end

    # bidi_streamer sends a stream of requests to the GRPC server, and yields
    # a stream of responses.
    #
    # This method takes an Enumerable of requests, and returns and enumerable
    # of responses.
    #
    # == requests ==
    #
    # requests provides an 'iterable' of Requests. I.e. it follows Ruby's
    # #each enumeration protocol. In the simplest case, requests will be an
    # array of marshallable objects; in typical case it will be an
    # Enumerable that allows dynamic construction of the marshallable
    # objects.
    #
    # == responses ==
    #
    # This is an enumerator of responses.  I.e, its #next method blocks
    # waiting for the next response.  Also, if at any point the block needs
    # to consume all the remaining responses, this can be done using #each or
    # #collect.  Calling #each or #collect should only be done if
    # the_call#writes_done has been called, otherwise the block will loop
    # forever.
    #
    # == Keyword Arguments ==
    # any keyword arguments are treated as metadata to be sent to the server
    # if a keyword value is a list, multiple metadata for it's key are sent
    #
    # @param requests [Object] an Enumerable of requests to send
    # @return [Enumerator, nil] a response Enumerator
    def bidi_streamer(requests, **kw, &blk)
      start_call(**kw) unless @started
      bd = BidiCall.new(@call, @cq, @marshal, @unmarshal, @deadline,
                        @finished_tag)
      bd.run_on_client(requests, &blk)
    end

    # run_server_bidi orchestrates a BiDi stream processing on a server.
    #
    # N.B. gen_each_reply is a func(Enumerable<Requests>)
    #
    # It takes an enumerable of requests as an arg, in case there is a
    # relationship between the stream of requests and the stream of replies.
    #
    # This does not mean that must necessarily be one.  E.g, the replies
    # produced by gen_each_reply could ignore the received_msgs
    #
    # @param gen_each_reply [Proc] generates the BiDi stream replies
    def run_server_bidi(gen_each_reply)
      bd = BidiCall.new(@call, @cq, @marshal, @unmarshal, @deadline,
                        @finished_tag)
      bd.run_on_server(gen_each_reply)
    end

    private

    def start_call(**kw)
      tags = ActiveCall.client_invoke(@call, @cq, @deadline, **kw)
      @finished_tag, @read_metadata_tag = tags
      @started = true
    end

    def self.view_class(*visible_methods)
      Class.new do
        extend ::Forwardable
        def_delegators :@wrapped, *visible_methods

        # @param wrapped [ActiveCall] the call whose methods are shielded
        def initialize(wrapped)
          @wrapped = wrapped
        end
      end
    end

    # SingleReqView limits access to an ActiveCall's methods for use in server
    # handlers that receive just one request.
    SingleReqView = view_class(:cancelled, :deadline, :metadata)

    # MultiReqView limits access to an ActiveCall's methods for use in
    # server client_streamer handlers.
    MultiReqView = view_class(:cancelled, :deadline, :each_queued_msg,
                              :each_remote_read, :metadata)

    # Operation limits access to an ActiveCall's methods for use as
    # a Operation on the client.
    Operation = view_class(:cancel, :cancelled, :deadline, :execute,
                           :metadata, :status)

    # confirms that no events are enqueued, and that the queue is not
    # shutdown.
    def assert_queue_is_ready
      ev = nil
      begin
        ev = @cq.pluck(self, ZERO)
        fail "unexpected event #{ev.inspect}" unless ev.nil?
      rescue OutOfTime
        logging.debug('timed out waiting for next event')
        # expected, nothing should be on the queue and the deadline was ZERO,
        # except things using another tag
      ensure
        ev.close unless ev.nil?
      end
    end
  end
end
