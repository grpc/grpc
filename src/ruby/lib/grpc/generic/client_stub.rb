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

require_relative 'active_call'
require_relative '../version'

# GRPC contains the General RPC module.
module GRPC
  # rubocop:disable Metrics/ParameterLists

  # ClientStub represents a client connection to a gRPC server, and can be used
  # to send requests.
  class ClientStub
    include Core::StatusCodes
    include Core::TimeConsts

    # Default timeout is infinity.
    DEFAULT_TIMEOUT = INFINITE_FUTURE

    # setup_channel is used by #initialize to constuct a channel from its
    # arguments.
    def self.setup_channel(alt_chan, host, creds, channel_args = {})
      unless alt_chan.nil?
        fail(TypeError, '!Channel') unless alt_chan.is_a?(Core::Channel)
        return alt_chan
      end
      if channel_args['grpc.primary_user_agent'].nil?
        channel_args['grpc.primary_user_agent'] = ''
      else
        channel_args['grpc.primary_user_agent'] += ' '
      end
      channel_args['grpc.primary_user_agent'] += "grpc-ruby/#{VERSION}"
      unless creds.is_a?(Core::ChannelCredentials) || creds.is_a?(Symbol)
        fail(TypeError, '!ChannelCredentials or Symbol')
      end
      Core::Channel.new(host, channel_args, creds)
    end

    # Allows users of the stub to modify the propagate mask.
    #
    # This is an advanced feature for use when making calls to another gRPC
    # server whilst running in the handler of an existing one.
    attr_writer :propagate_mask

    # Creates a new ClientStub.
    #
    # Minimally, a stub is created with the just the host of the gRPC service
    # it wishes to access, e.g.,
    #
    #   my_stub = ClientStub.new(example.host.com:50505,
    #                            :this_channel_is_insecure)
    #
    # If a channel_override argument is passed, it will be used as the
    # underlying channel. Otherwise, the channel_args argument will be used
    # to construct a new underlying channel.
    #
    # There are some specific keyword args that are not used to configure the
    # channel:
    #
    # - :channel_override
    # when present, this must be a pre-created GRPC::Core::Channel.  If it's
    # present the host and arbitrary keyword arg areignored, and the RPC
    # connection uses this channel.
    #
    # - :timeout
    # when present, this is the default timeout used for calls
    #
    # @param host [String] the host the stub connects to
    # @param creds [Core::ChannelCredentials|Symbol] the channel credentials, or
    #     :this_channel_is_insecure, which explicitly indicates that the client
    #     should be created with an insecure connection. Note: this argument is
    #     ignored if the channel_override argument is provided.
    # @param channel_override [Core::Channel] a pre-created channel
    # @param timeout [Number] the default timeout to use in requests
    # @param propagate_mask [Number] A bitwise combination of flags in
    #     GRPC::Core::PropagateMasks. Indicates how data should be propagated
    #     from parent server calls to child client calls if this client is being
    #     used within a gRPC server.
    # @param channel_args [Hash] the channel arguments. Note: this argument is
    #     ignored if the channel_override argument is provided.
    # @param interceptors [Array<GRPC::ClientInterceptor>] An array of
    #     GRPC::ClientInterceptor objects that will be used for
    #     intercepting calls before they are executed
    #     Interceptors are an EXPERIMENTAL API.
    def initialize(host, creds,
                   channel_override: nil,
                   timeout: nil,
                   propagate_mask: nil,
                   channel_args: {},
                   interceptors: [])
      @ch = ClientStub.setup_channel(channel_override, host, creds,
                                     channel_args)
      alt_host = channel_args[Core::Channel::SSL_TARGET]
      @host = alt_host.nil? ? host : alt_host
      @propagate_mask = propagate_mask
      @timeout = timeout.nil? ? DEFAULT_TIMEOUT : timeout
      @interceptors = InterceptorRegistry.new(interceptors)
    end

    # request_response sends a request to a GRPC server, and returns the
    # response.
    #
    # == Flow Control ==
    # This is a blocking call.
    #
    # * it does not return until a response is received.
    #
    # * the requests is sent only when GRPC core's flow control allows it to
    #   be sent.
    #
    # == Errors ==
    # An RuntimeError is raised if
    #
    # * the server responds with a non-OK status
    #
    # * the deadline is exceeded
    #
    # == Return Value ==
    #
    # If return_op is false, the call returns the response
    #
    # If return_op is true, the call returns an Operation, calling execute
    # on the Operation returns the response.
    #
    # @param method [String] the RPC method to call on the GRPC server
    # @param req [Object] the request sent to the server
    # @param marshal [Function] f(obj)->string that marshals requests
    # @param unmarshal [Function] f(string)->obj that unmarshals responses
    # @param deadline [Time] (optional) the time the request should complete
    # @param return_op [true|false] return an Operation if true
    # @param parent [Core::Call] a prior call whose reserved metadata
    #   will be propagated by this one.
    # @param credentials [Core::CallCredentials] credentials to use when making
    #   the call
    # @param metadata [Hash] metadata to be sent to the server
    # @return [Object] the response received from the server
    def request_response(method, req, marshal, unmarshal,
                         deadline: nil,
                         return_op: false,
                         parent: nil,
                         credentials: nil,
                         metadata: {})
      c = new_active_call(method, marshal, unmarshal,
                          deadline: deadline,
                          parent: parent,
                          credentials: credentials)
      interception_context = @interceptors.build_context
      intercept_args = {
        method: method,
        request: req,
        call: c.interceptable,
        metadata: metadata
      }
      if return_op
        # return the operation view of the active_call; define #execute as a
        # new method for this instance that invokes #request_response.
        c.merge_metadata_to_send(metadata)
        op = c.operation
        op.define_singleton_method(:execute) do
          interception_context.intercept!(:request_response, intercept_args) do
            c.request_response(req, metadata: metadata)
          end
        end
        op
      else
        interception_context.intercept!(:request_response, intercept_args) do
          c.request_response(req, metadata: metadata)
        end
      end
    end

    # client_streamer sends a stream of requests to a GRPC server, and
    # returns a single response.
    #
    # requests provides an 'iterable' of Requests. I.e. it follows Ruby's
    # #each enumeration protocol. In the simplest case, requests will be an
    # array of marshallable objects; in typical case it will be an Enumerable
    # that allows dynamic construction of the marshallable objects.
    #
    # == Flow Control ==
    # This is a blocking call.
    #
    # * it does not return until a response is received.
    #
    # * each requests is sent only when GRPC core's flow control allows it to
    #   be sent.
    #
    # == Errors ==
    # An RuntimeError is raised if
    #
    # * the server responds with a non-OK status
    #
    # * the deadline is exceeded
    #
    # == Return Value ==
    #
    # If return_op is false, the call consumes the requests and returns
    # the response.
    #
    # If return_op is true, the call returns the response.
    #
    # @param method [String] the RPC method to call on the GRPC server
    # @param requests [Object] an Enumerable of requests to send
    # @param marshal [Function] f(obj)->string that marshals requests
    # @param unmarshal [Function] f(string)->obj that unmarshals responses
    # @param deadline [Time] (optional) the time the request should complete
    # @param return_op [true|false] return an Operation if true
    # @param parent [Core::Call] a prior call whose reserved metadata
    #   will be propagated by this one.
    # @param credentials [Core::CallCredentials] credentials to use when making
    #   the call
    # @param metadata [Hash] metadata to be sent to the server
    # @return [Object|Operation] the response received from the server
    def client_streamer(method, requests, marshal, unmarshal,
                        deadline: nil,
                        return_op: false,
                        parent: nil,
                        credentials: nil,
                        metadata: {})
      c = new_active_call(method, marshal, unmarshal,
                          deadline: deadline,
                          parent: parent,
                          credentials: credentials)
      interception_context = @interceptors.build_context
      intercept_args = {
        method: method,
        requests: requests,
        call: c.interceptable,
        metadata: metadata
      }
      if return_op
        # return the operation view of the active_call; define #execute as a
        # new method for this instance that invokes #client_streamer.
        c.merge_metadata_to_send(metadata)
        op = c.operation
        op.define_singleton_method(:execute) do
          interception_context.intercept!(:client_streamer, intercept_args) do
            c.client_streamer(requests)
          end
        end
        op
      else
        interception_context.intercept!(:client_streamer, intercept_args) do
          c.client_streamer(requests, metadata: metadata)
        end
      end
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
    # == Flow Control ==
    # This is a blocking call.
    #
    # * the request is sent only when GRPC core's flow control allows it to
    #   be sent.
    #
    # * the request will not complete until the server sends the final
    #   response followed by a status message.
    #
    # == Errors ==
    # An RuntimeError is raised if
    #
    # * the server responds with a non-OK status when any response is
    # * retrieved
    #
    # * the deadline is exceeded
    #
    # == Return Value ==
    #
    # if the return_op is false, the return value is an Enumerator of the
    # results, unless a block is provided, in which case the block is
    # executed with each response.
    #
    # if return_op is true, the function returns an Operation whose #execute
    # method runs server streamer call. Again, Operation#execute either
    # calls the given block with each response or returns an Enumerator of the
    # responses.
    #
    # == Keyword Args ==
    #
    # Unspecified keyword arguments are treated as metadata to be sent to the
    # server.
    #
    # @param method [String] the RPC method to call on the GRPC server
    # @param req [Object] the request sent to the server
    # @param marshal [Function] f(obj)->string that marshals requests
    # @param unmarshal [Function] f(string)->obj that unmarshals responses
    # @param deadline [Time] (optional) the time the request should complete
    # @param return_op [true|false]return an Operation if true
    # @param parent [Core::Call] a prior call whose reserved metadata
    #   will be propagated by this one.
    # @param credentials [Core::CallCredentials] credentials to use when making
    #   the call
    # @param metadata [Hash] metadata to be sent to the server
    # @param blk [Block] when provided, is executed for each response
    # @return [Enumerator|Operation|nil] as discussed above
    def server_streamer(method, req, marshal, unmarshal,
                        deadline: nil,
                        return_op: false,
                        parent: nil,
                        credentials: nil,
                        metadata: {},
                        &blk)
      c = new_active_call(method, marshal, unmarshal,
                          deadline: deadline,
                          parent: parent,
                          credentials: credentials)
      interception_context = @interceptors.build_context
      intercept_args = {
        method: method,
        request: req,
        call: c.interceptable,
        metadata: metadata
      }
      if return_op
        # return the operation view of the active_call; define #execute
        # as a new method for this instance that invokes #server_streamer
        c.merge_metadata_to_send(metadata)
        op = c.operation
        op.define_singleton_method(:execute) do
          interception_context.intercept!(:server_streamer, intercept_args) do
            c.server_streamer(req, &blk)
          end
        end
        op
      else
        interception_context.intercept!(:server_streamer, intercept_args) do
          c.server_streamer(req, metadata: metadata, &blk)
        end
      end
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
    # == Flow Control ==
    # This is a blocking call.
    #
    # * the call completes when the next call to provided block returns
    #   false
    #
    # * the execution block parameters are two objects for sending and
    #   receiving responses, each of which blocks waiting for flow control.
    #   E.g, calles to bidi_call#remote_send will wait until flow control
    #   allows another write before returning; and obviously calls to
    #   responses#next block until the next response is available.
    #
    # == Termination ==
    #
    # As well as sending and receiving messages, the block passed to the
    # function is also responsible for:
    #
    # * calling bidi_call#writes_done to indicate no further reqs will be
    #   sent.
    #
    # * returning false if once the bidi stream is functionally completed.
    #
    # Note that response#next will indicate that there are no further
    # responses by throwing StopIteration, but can only happen either
    # if bidi_call#writes_done is called.
    #
    # To properly terminate the RPC, the responses should be completely iterated
    # through; one way to do this is to loop on responses#next until no further
    # responses are available.
    #
    # == Errors ==
    # An RuntimeError is raised if
    #
    # * the server responds with a non-OK status when any response is
    # * retrieved
    #
    # * the deadline is exceeded
    #
    #
    # == Return Value ==
    #
    # if the return_op is false, the return value is an Enumerator of the
    # results, unless a block is provided, in which case the block is
    # executed with each response.
    #
    # if return_op is true, the function returns an Operation whose #execute
    # method runs the Bidi call. Again, Operation#execute either calls a
    # given block with each response or returns an Enumerator of the
    # responses.
    #
    # @param method [String] the RPC method to call on the GRPC server
    # @param requests [Object] an Enumerable of requests to send
    # @param marshal [Function] f(obj)->string that marshals requests
    # @param unmarshal [Function] f(string)->obj that unmarshals responses
    # @param deadline [Time] (optional) the time the request should complete
    # @param return_op [true|false] return an Operation if true
    # @param parent [Core::Call] a prior call whose reserved metadata
    #   will be propagated by this one.
    # @param credentials [Core::CallCredentials] credentials to use when making
    #   the call
    # @param metadata [Hash] metadata to be sent to the server
    # @param blk [Block] when provided, is executed for each response
    # @return [Enumerator|nil|Operation] as discussed above
    def bidi_streamer(method, requests, marshal, unmarshal,
                      deadline: nil,
                      return_op: false,
                      parent: nil,
                      credentials: nil,
                      metadata: {},
                      &blk)
      c = new_active_call(method, marshal, unmarshal,
                          deadline: deadline,
                          parent: parent,
                          credentials: credentials)
      interception_context = @interceptors.build_context
      intercept_args = {
        method: method,
        requests: requests,
        call: c.interceptable,
        metadata: metadata
      }
      if return_op
        # return the operation view of the active_call; define #execute
        # as a new method for this instance that invokes #bidi_streamer
        c.merge_metadata_to_send(metadata)
        op = c.operation
        op.define_singleton_method(:execute) do
          interception_context.intercept!(:bidi_streamer, intercept_args) do
            c.bidi_streamer(requests, &blk)
          end
        end
        op
      else
        interception_context.intercept!(:bidi_streamer, intercept_args) do
          c.bidi_streamer(requests, metadata: metadata, &blk)
        end
      end
    end

    private

    # Creates a new active stub
    #
    # @param method [string] the method being called.
    # @param marshal [Function] f(obj)->string that marshals requests
    # @param unmarshal [Function] f(string)->obj that unmarshals responses
    # @param parent [Grpc::Call] a parent call, available when calls are
    #   made from server
    # @param credentials [Core::CallCredentials] credentials to use when making
    #   the call
    def new_active_call(method, marshal, unmarshal,
                        deadline: nil,
                        parent: nil,
                        credentials: nil)
      deadline = from_relative_time(@timeout) if deadline.nil?
      # Provide each new client call with its own completion queue
      call = @ch.create_call(parent, # parent call
                             @propagate_mask, # propagation options
                             method,
                             nil, # host use nil,
                             deadline)
      call.set_credentials! credentials unless credentials.nil?
      ActiveCall.new(call, marshal, unmarshal, deadline,
                     started: false)
    end
  end
end
