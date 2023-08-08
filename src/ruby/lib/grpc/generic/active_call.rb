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
require 'weakref'
require_relative 'bidi_call'

class Struct
  # BatchResult is the struct returned by calls to call#start_batch.
  class BatchResult
    # check_status returns the status, raising an error if the status
    # is non-nil and not OK.
    def check_status
      return nil if status.nil?
      if status.code != GRPC::Core::StatusCodes::OK
        GRPC.logger.debug("Failing with status #{status}")
        # raise BadStatus, propagating the metadata if present.
        fail GRPC::BadStatus.new_status_exception(
          status.code, status.details, status.metadata,
          status.debug_error_string)
      end
      status
    end
  end
end

# GRPC contains the General RPC module.
module GRPC
  # The ActiveCall class provides simple methods for sending marshallable
  # data to a call
  class ActiveCall # rubocop:disable Metrics/ClassLength
    include Core::TimeConsts
    include Core::CallOps
    extend Forwardable
    attr_reader :deadline, :metadata_sent, :metadata_to_send, :peer, :peer_cert
    def_delegators :@call, :cancel, :metadata, :write_flag, :write_flag=,
                   :trailing_metadata, :status

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

      @output_stream_done = false
      @input_stream_done = false
      @call_finished = false
      @call_finished_mu = Mutex.new

      @client_call_executed = false
      @client_call_executed_mu = Mutex.new

      # set the peer now so that the accessor can still function
      # after the server closes the call
      @peer = call.peer
    end

    # Sends the initial metadata that has yet to be sent.
    # Does nothing if metadata has already been sent for this call.
    def send_initial_metadata(new_metadata = {})
      @send_initial_md_mutex.synchronize do
        return if @metadata_sent
        @metadata_to_send.merge!(new_metadata)
        ActiveCall.client_invoke(@call, @metadata_to_send)
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

    ##
    # Returns a restricted view of this ActiveCall for use in interceptors
    #
    # @return [InterceptableView]
    #
    def interceptable
      InterceptableView.new(self)
    end

    def receive_and_check_status
      ops = { RECV_STATUS_ON_CLIENT => nil }
      ops[RECV_INITIAL_METADATA] = nil unless @metadata_received
      batch_result = @call.run_batch(ops)
      unless @metadata_received
        @call.metadata = batch_result.metadata
        @metadata_received = true
      end
      set_input_stream_done
      attach_status_results_and_complete_call(batch_result)
    end

    def attach_status_results_and_complete_call(recv_status_batch_result)
      unless recv_status_batch_result.status.nil?
        @call.trailing_metadata = recv_status_batch_result.status.metadata
      end
      @call.status = recv_status_batch_result.status

      # The RECV_STATUS in run_batch always succeeds
      # Check the status for a bad status or failed run batch
      recv_status_batch_result.check_status
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
      set_output_stream_done

      nil
    end

    # Intended for use on server-side calls when a single request from
    # the client is expected (i.e., unary and server-streaming RPC types).
    def read_unary_request
      req = remote_read
      set_input_stream_done
      req
    end

    def server_unary_response(req, trailing_metadata: {},
                              code: Core::StatusCodes::OK, details: 'OK')
      ops = {}
      @send_initial_md_mutex.synchronize do
        ops[SEND_INITIAL_METADATA] = @metadata_to_send unless @metadata_sent
        @metadata_sent = true
      end

      payload = @marshal.call(req)
      ops[SEND_MESSAGE] = payload
      ops[SEND_STATUS_FROM_SERVER] = Struct::Status.new(
        code, details, trailing_metadata)
      ops[RECV_CLOSE_ON_SERVER] = nil

      @call.run_batch(ops)
      set_output_stream_done
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
      get_message_from_batch_result(batch_result)
    rescue GRPC::Core::CallError => e
      GRPC.logger.info("remote_read: #{e}")
      nil
    end

    def get_message_from_batch_result(recv_message_batch_result)
      unless recv_message_batch_result.nil? ||
             recv_message_batch_result.message.nil?
        return @unmarshal.call(recv_message_batch_result.message)
      end
      GRPC.logger.debug('found nil; the final response has been sent')
      nil
    end

    # each_remote_read passes each response to the given block or returns an
    # enumerator the responses if no block is given.
    # Used to generate the request enumerable for
    # server-side client-streaming RPC's.
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
      begin
        loop do
          resp = remote_read
          break if resp.nil?  # the last response was received
          yield resp
        end
      ensure
        set_input_stream_done
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
        break if resp.nil?  # the last response was received
        yield resp
      end

      receive_and_check_status
    ensure
      set_input_stream_done
    end

    # request_response sends a request to a GRPC server, and returns the
    # response.
    #
    # @param req [Object] the request sent to the server
    # @param metadata [Hash] metadata to be sent to the server. If a value is
    # a list, multiple metadata for its key are sent
    # @return [Object] the response received from the server
    def request_response(req, metadata: {})
      raise_error_if_already_executed
      ops = {
        SEND_MESSAGE => @marshal.call(req),
        SEND_CLOSE_FROM_CLIENT => nil,
        RECV_INITIAL_METADATA => nil,
        RECV_MESSAGE => nil,
        RECV_STATUS_ON_CLIENT => nil
      }
      @send_initial_md_mutex.synchronize do
        # Metadata might have already been sent if this is an operation view
        unless @metadata_sent
          ops[SEND_INITIAL_METADATA] = @metadata_to_send.merge!(metadata)
        end
        @metadata_sent = true
      end

      begin
        batch_result = @call.run_batch(ops)
        # no need to check for cancellation after a CallError because this
        # batch contains a RECV_STATUS op
      ensure
        set_input_stream_done
        set_output_stream_done
      end

      @call.metadata = batch_result.metadata
      attach_status_results_and_complete_call(batch_result)
      get_message_from_batch_result(batch_result)
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
      raise_error_if_already_executed
      begin
        send_initial_metadata(metadata)
        requests.each { |r| @call.run_batch(SEND_MESSAGE => @marshal.call(r)) }
      rescue GRPC::Core::CallError => e
        receive_and_check_status # check for Cancelled
        raise e
      rescue => e
        set_input_stream_done
        raise e
      ensure
        set_output_stream_done
      end

      batch_result = @call.run_batch(
        SEND_CLOSE_FROM_CLIENT => nil,
        RECV_INITIAL_METADATA => nil,
        RECV_MESSAGE => nil,
        RECV_STATUS_ON_CLIENT => nil
      )

      set_input_stream_done

      @call.metadata = batch_result.metadata
      attach_status_results_and_complete_call(batch_result)
      get_message_from_batch_result(batch_result)
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
      raise_error_if_already_executed
      ops = {
        SEND_MESSAGE => @marshal.call(req),
        SEND_CLOSE_FROM_CLIENT => nil
      }
      @send_initial_md_mutex.synchronize do
        # Metadata might have already been sent if this is an operation view
        unless @metadata_sent
          ops[SEND_INITIAL_METADATA] = @metadata_to_send.merge!(metadata)
        end
        @metadata_sent = true
      end

      begin
        @call.run_batch(ops)
      rescue GRPC::Core::CallError => e
        receive_and_check_status # checks for Cancelled
        raise e
      rescue => e
        set_input_stream_done
        raise e
      ensure
        set_output_stream_done
      end

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
    # @param requests [Object] an Enumerable of requests to send
    # @param metadata [Hash] metadata to be sent to the server. If a value is
    # a list, multiple metadata for its key are sent
    # @return [Enumerator, nil] a response Enumerator
    def bidi_streamer(requests, metadata: {}, &blk)
      raise_error_if_already_executed
      # Metadata might have already been sent if this is an operation view
      begin
        send_initial_metadata(metadata)
      rescue GRPC::Core::CallError => e
        batch_result = @call.run_batch(RECV_STATUS_ON_CLIENT => nil)
        set_input_stream_done
        set_output_stream_done
        attach_status_results_and_complete_call(batch_result)
        raise e
      rescue => e
        set_input_stream_done
        set_output_stream_done
        raise e
      end

      bd = BidiCall.new(@call,
                        @marshal,
                        @unmarshal,
                        metadata_received: @metadata_received)

      bd.run_on_client(requests,
                       proc { set_input_stream_done },
                       proc { set_output_stream_done },
                       &blk)
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
    # @param mth [Proc] generates the BiDi stream replies
    # @param interception_ctx [InterceptionContext]
    #
    def run_server_bidi(mth, interception_ctx)
      view = multi_req_view
      bidi_call = BidiCall.new(
        @call,
        @marshal,
        @unmarshal,
        metadata_received: @metadata_received,
        req_view: view
      )
      requests = bidi_call.read_next_loop(proc { set_input_stream_done }, false)
      interception_ctx.intercept!(
        :bidi_streamer,
        call: view,
        method: mth,
        requests: requests
      ) do
        bidi_call.run_on_server(mth, requests)
      end
    end

    # Waits till an operation completes
    def wait
      return if @op_notifier.nil?
      GRPC.logger.debug("active_call.wait: on #{@op_notifier}")
      @op_notifier.wait
    end

    # Signals that an operation is done.
    # Only relevant on the client-side (this is a no-op on the server-side)
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

    def attach_peer_cert(peer_cert)
      @peer_cert = peer_cert
    end

    private

    # To be called once the "input stream" has been completelly
    # read through (i.e, done reading from client or received status)
    # note this is idempotent
    def set_input_stream_done
      @call_finished_mu.synchronize do
        @input_stream_done = true
        maybe_finish_and_close_call_locked
      end
    end

    # To be called once the "output stream" has been completelly
    # sent through (i.e, done sending from client or sent status)
    # note this is idempotent
    def set_output_stream_done
      @call_finished_mu.synchronize do
        @output_stream_done = true
        maybe_finish_and_close_call_locked
      end
    end

    def maybe_finish_and_close_call_locked
      return unless @output_stream_done && @input_stream_done
      return if @call_finished
      @call_finished = true
      op_is_done
      @call.close
    end

    # Starts the call if not already started
    # @param metadata [Hash] metadata to be sent to the server. If a value is
    # a list, multiple metadata for its key are sent
    def start_call(metadata = {})
      merge_metadata_to_send(metadata) && send_initial_metadata
    end

    def raise_error_if_already_executed
      @client_call_executed_mu.synchronize do
        if @client_call_executed
          fail GRPC::Core::CallError, 'attempting to re-run a call'
        end
        @client_call_executed = true
      end
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
    MultiReqView = view_class(:cancelled?, :deadline,
                              :each_remote_read, :metadata, :output_metadata,
                              :peer, :peer_cert,
                              :send_initial_metadata,
                              :metadata_to_send,
                              :merge_metadata_to_send,
                              :metadata_sent)

    # Operation limits access to an ActiveCall's methods for use as
    # a Operation on the client.
    Operation = view_class(:cancel, :cancelled?, :deadline, :execute,
                           :metadata, :status, :start_call, :wait, :write_flag,
                           :write_flag=, :trailing_metadata)

    # InterceptableView further limits access to an ActiveCall's methods
    # for use in interceptors on the client, exposing only the deadline
    InterceptableView = view_class(:deadline)
  end
end
