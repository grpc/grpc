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

require 'grpc/generic/active_call'
require 'grpc/version'

# GRPC contains the General RPC module.
module GRPC
  # rubocop:disable Metrics/ParameterLists

  # ClientStub represents an endpoint used to send requests to GRPC servers.
  class ClientStub
    include Core::StatusCodes
    include Core::TimeConsts

    # Default timeout is infinity.
    DEFAULT_TIMEOUT = INFINITE_FUTURE

    # setup_channel is used by #initialize to constuct a channel from its
    # arguments.
    def self.setup_channel(alt_chan, host, creds, **kw)
      unless alt_chan.nil?
        fail(TypeError, '!Channel') unless alt_chan.is_a?(Core::Channel)
        return alt_chan
      end
      kw['grpc.primary_user_agent'] = "grpc-ruby/#{VERSION}"
      return Core::Channel.new(host, kw) if creds.nil?
      fail(TypeError, '!Credentials') unless creds.is_a?(Core::Credentials)
      Core::Channel.new(host, kw, creds)
    end

    def self.update_with_jwt_aud_uri(a_hash, host, method)
      last_slash_idx, res = method.rindex('/'), a_hash.clone
      return res if last_slash_idx.nil?
      service_name = method[0..(last_slash_idx - 1)]
      res[:jwt_aud_uri] = "https://#{host}#{service_name}"
      res
    end

    # check_update_metadata is used by #initialize verify that it's a Proc.
    def self.check_update_metadata(update_metadata)
      return update_metadata if update_metadata.nil?
      fail(TypeError, '!is_a?Proc') unless update_metadata.is_a?(Proc)
      update_metadata
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
    # my_stub = ClientStub.new(example.host.com:50505)
    #
    # Any arbitrary keyword arguments are treated as channel arguments used to
    # configure the RPC connection to the host.
    #
    # There are some specific keyword args that are not used to configure the
    # channel:
    #
    # - :channel_override
    # when present, this must be a pre-created GRPC::Channel.  If it's
    # present the host and arbitrary keyword arg areignored, and the RPC
    # connection uses this channel.
    #
    # - :timeout
    # when present, this is the default timeout used for calls
    #
    # - :update_metadata
    # when present, this a func that takes a hash and returns a hash
    # it can be used to update metadata, i.e, remove, or amend
    # metadata values.
    #
    # @param host [String] the host the stub connects to
    # @param q [Core::CompletionQueue] used to wait for events
    # @param channel_override [Core::Channel] a pre-created channel
    # @param timeout [Number] the default timeout to use in requests
    # @param creds [Core::Credentials] the channel
    # @param update_metadata a func that updates metadata as described above
    # @param kw [KeywordArgs]the channel arguments
    def initialize(host, q,
                   channel_override: nil,
                   timeout: nil,
                   creds: nil,
                   propagate_mask: nil,
                   update_metadata: nil,
                   **kw)
      fail(TypeError, '!CompletionQueue') unless q.is_a?(Core::CompletionQueue)
      @queue = q
      @ch = ClientStub.setup_channel(channel_override, host, creds, **kw)
      @update_metadata = ClientStub.check_update_metadata(update_metadata)
      alt_host = kw[Core::Channel::SSL_TARGET]
      @host = alt_host.nil? ? host : alt_host
      @propagate_mask = propagate_mask
      @timeout = timeout.nil? ? DEFAULT_TIMEOUT : timeout
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
    # == Keyword Args ==
    #
    # Unspecified keyword arguments are treated as metadata to be sent to the
    # server.
    #
    # @param method [String] the RPC method to call on the GRPC server
    # @param req [Object] the request sent to the server
    # @param marshal [Function] f(obj)->string that marshals requests
    # @param unmarshal [Function] f(string)->obj that unmarshals responses
    # @param timeout [Numeric] (optional) the max completion time in seconds
    # @param deadline [Time] (optional) the time the request should complete
    # @param parent [Core::Call] a prior call whose reserved metadata
    #   will be propagated by this one.
    # @param return_op [true|false] return an Operation if true
    # @return [Object] the response received from the server
    def request_response(method, req, marshal, unmarshal,
                         deadline: nil,
                         timeout: nil,
                         return_op: false,
                         parent: nil,
                         **kw)
      c = new_active_call(method, marshal, unmarshal,
                          deadline: deadline,
                          timeout: timeout,
                          parent: parent)
      md = update_metadata(kw, method)
      return c.request_response(req, **md) unless return_op

      # return the operation view of the active_call; define #execute as a
      # new method for this instance that invokes #request_response.
      op = c.operation
      op.define_singleton_method(:execute) do
        c.request_response(req, **md)
      end
      op
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
    # == Keyword Args ==
    #
    # Unspecified keyword arguments are treated as metadata to be sent to the
    # server.
    #
    # @param method [String] the RPC method to call on the GRPC server
    # @param requests [Object] an Enumerable of requests to send
    # @param marshal [Function] f(obj)->string that marshals requests
    # @param unmarshal [Function] f(string)->obj that unmarshals responses
    # @param timeout [Numeric] (optional) the max completion time in seconds
    # @param deadline [Time] (optional) the time the request should complete
    # @param return_op [true|false] return an Operation if true
    # @param parent [Core::Call] a prior call whose reserved metadata
    #   will be propagated by this one.
    # @return [Object|Operation] the response received from the server
    def client_streamer(method, requests, marshal, unmarshal,
                        deadline: nil,
                        timeout: nil,
                        return_op: false,
                        parent: nil,
                        **kw)
      c = new_active_call(method, marshal, unmarshal,
                          deadline: deadline,
                          timeout: timeout,
                          parent: parent)
      md = update_metadata(kw, method)
      return c.client_streamer(requests, **md) unless return_op

      # return the operation view of the active_call; define #execute as a
      # new method for this instance that invokes #client_streamer.
      op = c.operation
      op.define_singleton_method(:execute) do
        c.client_streamer(requests, **md)
      end
      op
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
    # @param timeout [Numeric] (optional) the max completion time in seconds
    # @param deadline [Time] (optional) the time the request should complete
    # @param return_op [true|false]return an Operation if true
    # @param parent [Core::Call] a prior call whose reserved metadata
    #   will be propagated by this one.
    # @param blk [Block] when provided, is executed for each response
    # @return [Enumerator|Operation|nil] as discussed above
    def server_streamer(method, req, marshal, unmarshal,
                        deadline: nil,
                        timeout: nil,
                        return_op: false,
                        parent: nil,
                        **kw,
                        &blk)
      c = new_active_call(method, marshal, unmarshal,
                          deadline: deadline,
                          timeout: timeout,
                          parent: parent)
      md = update_metadata(kw, method)
      return c.server_streamer(req, **md, &blk) unless return_op

      # return the operation view of the active_call; define #execute
      # as a new method for this instance that invokes #server_streamer
      op = c.operation
      op.define_singleton_method(:execute) do
        c.server_streamer(req, **md, &blk)
      end
      op
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
    # * [False]
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
    # To terminate the RPC correctly the block:
    #
    # * must call bidi#writes_done and then
    #
    #    * either return false as soon as there is no need for other responses
    #
    #    * loop on responses#next until no further responses are available
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
    # == Keyword Args ==
    #
    # Unspecified keyword arguments are treated as metadata to be sent to the
    # server.
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
    # @param timeout [Numeric] (optional) the max completion time in seconds
    # @param deadline [Time] (optional) the time the request should complete
    # @param parent [Core::Call] a prior call whose reserved metadata
    #   will be propagated by this one.
    # @param return_op [true|false] return an Operation if true
    # @param blk [Block] when provided, is executed for each response
    # @return [Enumerator|nil|Operation] as discussed above
    def bidi_streamer(method, requests, marshal, unmarshal,
                      deadline: nil,
                      timeout: nil,
                      return_op: false,
                      parent: nil,
                      **kw,
                      &blk)
      c = new_active_call(method, marshal, unmarshal,
                          deadline: deadline,
                          timeout: timeout,
                          parent: parent)
      md = update_metadata(kw, method)
      return c.bidi_streamer(requests, **md, &blk) unless return_op

      # return the operation view of the active_call; define #execute
      # as a new method for this instance that invokes #bidi_streamer
      op = c.operation
      op.define_singleton_method(:execute) do
        c.bidi_streamer(requests, **md, &blk)
      end
      op
    end

    private

    def update_metadata(kw, method)
      return kw if @update_metadata.nil?
      just_jwt_uri = self.class.update_with_jwt_aud_uri({}, @host, method)
      updated = @update_metadata.call(just_jwt_uri)

      # keys should be lowercase
      updated = Hash[updated.each_pair.map { |k, v|  [k.downcase, v] }]
      kw.merge(updated)
    end

    # Creates a new active stub
    #
    # @param method [string] the method being called.
    # @param marshal [Function] f(obj)->string that marshals requests
    # @param unmarshal [Function] f(string)->obj that unmarshals responses
    # @param parent [Grpc::Call] a parent call, available when calls are
    #   made from server
    # @param timeout [TimeConst]
    def new_active_call(method, marshal, unmarshal,
                        deadline: nil,
                        timeout: nil,
                        parent: nil)
      if deadline.nil?
        deadline = from_relative_time(timeout.nil? ? @timeout : timeout)
      end
      call = @ch.create_call(@queue,
                             parent, # parent call
                             @propagate_mask, # propagation options
                             method,
                             nil, # host use nil,
                             deadline)
      ActiveCall.new(call, @queue, marshal, unmarshal, deadline, started: false)
    end
  end
end
