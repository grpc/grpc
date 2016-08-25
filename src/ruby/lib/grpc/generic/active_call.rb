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
require 'weakref'
require_relative 'bidi_call'

class Struct
  # BatchResult is the struct returned by calls to call#start_batch.
  class BatchResult
    # check_status returns the status, raising an error if the status
    # is non-nil and not OK.
    def check_status
      return nil if status.nil?
      fail GRPC::Cancelled if status.code == GRPC::Core::StatusCodes::CANCELLED
      if status.code != GRPC::Core::StatusCodes::OK
        GRPC.logger.debug("Failing with status #{status}")
        # raise BadStatus, propagating the metadata if present.
        md = status.metadata
        fail GRPC::BadStatus.new(status.code, status.details, md)
      end
      status
    end
  end
end

# GRPC contains the General RPC module.
module GRPC
  # The ActiveCall class provides simple methods for sending marshallable
  # data to a call
  class ActiveCall
    include Core::TimeConsts
    include Core::CallOps
    extend Forwardable
    attr_reader :deadline, :metadata_sent, :metadata_to_send
    def_delegators :@call, :cancel, :metadata, :write_flag, :write_flag=,
                   :peer, :peer_cert, :trailing_metadata

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
    # @param metadata [Hash] the metadata
    def self.client_invoke(call, metadata = {})
      fail(TypeError, '!Core::Call') unless call.is_a? Core::Call
      call.run_batch(SEND_INITIAL_METADATA => metadata)
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
    # @param marshal [Function] f(obj)->string that marshal requests
    # @param unmarshal [Function] f(string)->obj that unmarshals responses
    # @param deadline [Fixnum] the deadline for the call to complete
    # @param started [true|false] indicates that metadata was sent
    # @param metadata_received [true|false] indicates if metadata has already
    #     been received. Should always be true for server calls
    def initialize(call, marshal, unmarshal, deadline, started: true,
                   metadata_received: false, metadata_to_send: nil)
      fail(TypeError, '!Core::Call') unless call.is_a? Core::Call
      @call = call
      @deadline = deadline
      @marshal = marshal
      @unmarshal = unmarshal
      @metadata_received = metadata_received
      @metadata_sent = started
      @op_notifier = nil

      fail(ArgumentError, 'Already sent md') if started && metadata_to_send
      @metadata_to_send = metadata_to_send || {} unless started
      @send_initial_md_mutex = Mutex.new
    end

    # Sends the initial metadata that has yet to be sent.
    # Does nothing if metadata has already been sent for this call.
    def send_initial_metadata
      @send_initial_md_mutex.synchronize do
        return if @metadata_sent
        @metadata_tag = ActiveCall.client_invoke(@call, @metadata_to_send)
        @metadata_sent = true
      end
    end

    # output_metadata are provides access to hash that can be used to
    # save metadata to be sent as trailer
    def output_metadata
      @output_metadata ||= {}
    end

    # cancelled indicates if the call was cancelled
    def cancelled?
      !@call.status.nil? && @call.status.code == Core::StatusCodes::CANCELLED
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
      @op_notifier = Notifier.new
      Operation.new(self)
    end

    # writes_done indicates that all writes are completed.
    #
    # It blocks until the remote endpoint acknowledges with at status unless
    # assert_finished is set to false.  Any calls to #remote_send after this
    # call will fail.
    #
    # @param assert_finished [true, false] when true(default), waits for
    # FINISHED.
    def writes_done(assert_finished = true)
      ops = {
        SEND_CLOSE_FROM_CLIENT => nil
      }
      ops[RECV_STATUS_ON_CLIENT] = nil if assert_finished
      batch_result = @call.run_batch(ops)
      return unless assert_finished
      unless batch_result.status.nil?
        @call.trailing_metadata = batch_result.status.metadata
      end
      @call.status = batch_result.status
      op_is_done
      batch_result.check_status
    end

    # finished waits until a client call is completed.
    #
    # It blocks until the remote endpoint acknowledges by sending a status.
    def finished
      batch_result = @call.run_batch(RECV_STATUS_ON_CLIENT => nil)
      unless batch_result.status.nil?
        @call.trailing_metadata = batch_result.status.metadata
      end
      @call.status = batch_result.status
      op_is_done
      batch_result.check_status
      @call.close
    end

    # remote_send sends a request to the remote endpoint.
    #
    # It blocks until the remote endpoint accepts the message.
    #
    # @param req [Object, String] the object to send or it's marshal form.
    # @param marshalled [false, true] indicates if the object is already
    # marshalled.
    def remote_send(req, marshalled = false)
      send_initial_metadata
      GRPC.logger.debug("sending #{req}, marshalled? #{marshalled}")
      payload = marshalled ? req : @marshal.call(req)
      @call.run_batch(SEND_MESSAGE => payload)
    end

    # send_status sends a status to the remote endpoint.
    #
    # @param code [int] the status code to send
    # @param details [String] details
    # @param assert_finished [true, false] when true(default), waits for
    # FINISHED.
    # @param metadata [Hash] metadata to send to the server. If a value is a
    # list, mulitple metadata for its key are sent
    def send_status(code = OK, details = '', assert_finished = false,
                    metadata: {})
      send_initial_metadata
      ops = {
        SEND_STATUS_FROM_SERVER => Struct::Status.new(code, details, metadata)
      }
      ops[RECV_CLOSE_ON_SERVER] = nil if assert_finished
      @call.run_batch(ops)
      nil
    end

    def server_unary_response(req, marshalled: false, trailing_metadata: {},
                              code: Core::StatusCodes::OK, details: 'OK')
      @send_initial_md_mutex.synchronize do
        ops = {}
        ops[SEND_INITIAL_METADATA] = @metadata_to_send unless @metadata_sent
        @metadata_sent = true unless @metadata_sent

        payload = marshalled ? req : @marshal.call(req)
        ops[SEND_MESSAGE] = payload
        ops[SEND_STATUS_FROM_SERVER] = Struct::Status.new(
          code, details, trailing_metadata)
        ops[RECV_CLOSE_ON_SERVER] = nil

        @call.run_batch(ops)
      end
    end

    # remote_read reads a response from the remote endpoint.
    #
    # It blocks until the remote endpoint replies with a message or status.
    # On receiving a message, it returns the response after unmarshalling it.
    # On receiving a status, it returns nil if the status is OK, otherwise
    # raising BadStatus
    def remote_read
      ops = { RECV_MESSAGE => nil }
      ops[RECV_INITIAL_METADATA] = nil unless @metadata_received
      batch_result = @call.run_batch(ops)
      unless @metadata_received
        @call.metadata = batch_result.metadata
        @metadata_received = true
      end
      unless batch_result.nil? || batch_result.message.nil?
        res = @unmarshal.call(batch_result.message)
        return res
      end
      GRPC.logger.debug('found nil; the final response has been sent')
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
    # @param req [Object] the request sent to the server
    # @param metadata [Hash] metadata to be sent to the server. If a value is
    # a list, multiple metadata for its key are sent
    # @return [Object] the response received from the server
    def request_response(req, metadata: {})
      batch_result = nil
      @send_initial_md_mutex.synchronize do
        fail 'md already sent' if @metadata_sent
        batch_result = @call.run_batch(
          SEND_INITIAL_METADATA => metadata,
          SEND_MESSAGE => @marshal.call(req),
          SEND_CLOSE_FROM_CLIENT => nil,
          RECV_INITIAL_METADATA => nil,
          RECV_MESSAGE => nil,
          RECV_STATUS_ON_CLIENT => nil)
        @metadata_sent = true
      end

      response = nil
      unless batch_result.nil? || batch_result.message.nil?
        response = @unmarshal.call(batch_result.message)
      end
      unless batch_result.status.nil?
        @call.trailing_metadata = batch_result.status.metadata
      end
      @call.status = batch_result.status
      @call.metadata = batch_result.metadata
      op_is_done

      # The RECV_STATUS in run_batch always succeeds
      # Check the status for a bad status or failed run batch
      batch_result.check_status
      @call.close

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
    # @param requests [Object] an Enumerable of requests to send
    # @param metadata [Hash] metadata to be sent to the server. If a value is
    # a list, multiple metadata for its key are sent
    # @return [Object] the response received from the server
    def client_streamer(requests, metadata: {})
      merge_metadata_to_send(metadata) && send_initial_metadata
      requests.each { |r| remote_send(r) }
      writes_done(false)
      response = remote_read
      finished unless response.is_a? Struct::Status
      response
    rescue GRPC::Core::CallError => e
      finished  # checks for Cancelled
      raise e
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
    # @param req [Object] the request sent to the server
    # @param metadata [Hash] metadata to be sent to the server. If a value is
    # a list, multiple metadata for its key are sent
    # @return [Enumerator|nil] a response Enumerator
    def server_streamer(req, metadata: {})
      merge_metadata_to_send(metadata) && send_initial_metadata
      remote_send(req)
      writes_done(false)
      replies = enum_for(:each_remote_read_then_finish)
      return replies unless block_given?
      replies.each { |r| yield r }
    rescue GRPC::Core::CallError => e
      finished  # checks for Cancelled
      raise e
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
    # @param requests [Object] an Enumerable of requests to send
    # @param metadata [Hash] metadata to be sent to the server. If a value is
    # a list, multiple metadata for its key are sent
    # @return [Enumerator, nil] a response Enumerator
    def bidi_streamer(requests, metadata: {}, &blk)
      merge_metadata_to_send(metadata) && send_initial_metadata
      bd = BidiCall.new(@call,
                        @marshal,
                        @unmarshal,
                        metadata_received: @metadata_received)

      bd.run_on_client(requests, @op_notifier, &blk)
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
      bd = BidiCall.new(@call,
                        @marshal,
                        @unmarshal,
                        metadata_received: @metadata_received,
                        req_view: MultiReqView.new(self))

      bd.run_on_server(gen_each_reply)
    end

    # Waits till an operation completes
    def wait
      return if @op_notifier.nil?
      GRPC.logger.debug("active_call.wait: on #{@op_notifier}")
      @op_notifier.wait
    end

    # Signals that an operation is done
    def op_is_done
      return if @op_notifier.nil?
      @op_notifier.notify(self)
    end

    # Add to the metadata that will be sent from the server.
    # Fails if metadata has already been sent.
    # Unused by client calls.
    def merge_metadata_to_send(new_metadata = {})
      @send_initial_md_mutex.synchronize do
        fail('cant change metadata after already sent') if @metadata_sent
        @metadata_to_send.merge!(new_metadata)
      end
    end

    private

    # Starts the call if not already started
    # @param metadata [Hash] metadata to be sent to the server. If a value is
    # a list, multiple metadata for its key are sent
    def start_call(metadata = {})
      merge_metadata_to_send(metadata) && send_initial_metadata
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
    SingleReqView = view_class(:cancelled?, :deadline, :metadata,
                               :output_metadata, :peer, :peer_cert,
                               :send_initial_metadata,
                               :metadata_to_send,
                               :merge_metadata_to_send,
                               :metadata_sent)

    # MultiReqView limits access to an ActiveCall's methods for use in
    # server client_streamer handlers.
    MultiReqView = view_class(:cancelled?, :deadline, :each_queued_msg,
                              :each_remote_read, :metadata, :output_metadata,
                              :send_initial_metadata,
                              :metadata_to_send,
                              :merge_metadata_to_send,
                              :metadata_sent)

    # Operation limits access to an ActiveCall's methods for use as
    # a Operation on the client.
    Operation = view_class(:cancel, :cancelled?, :deadline, :execute,
                           :metadata, :status, :start_call, :wait, :write_flag,
                           :write_flag=, :trailing_metadata)
  end
end
