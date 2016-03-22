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

require 'grpc/generic/client_stub'
require 'grpc/generic/rpc_desc'

# GRPC contains the General RPC module.
module GRPC
  # Provides behaviour used to implement schema-derived service classes.
  #
  # Is intended to be used to support both client and server
  # IDL-schema-derived servers.
  module GenericService
    # creates a new string that is the underscore separate version of s.
    #
    # E.g,
    # PrintHTML -> print_html
    # AMethod -> a_method
    # AnRpc -> an_rpc
    #
    # @param s [String] the string to be converted.
    def self.underscore(s)
      s.gsub!(/([A-Z]+)([A-Z][a-z])/, '\1_\2')
      s.gsub!(/([a-z\d])([A-Z])/, '\1_\2')
      s.tr!('-', '_')
      s.downcase!
      s
    end

    # Used to indicate that a name has already been specified
    class DuplicateRpcName < StandardError
      def initialize(name)
        super("rpc (#{name}) is already defined")
      end
    end

    # Provides a simple DSL to describe RPC services.
    #
    # E.g, a Maths service that uses the serializable messages DivArgs,
    # DivReply and Num might define its endpoint uses the following way:
    #
    # rpc :div DivArgs, DivReply    # single request, single response
    # rpc :sum stream(Num), Num     # streamed input, single response
    # rpc :fib FibArgs, stream(Num) # single request, streamed response
    # rpc :div_many stream(DivArgs), stream(DivReply)
    #                               # streamed req and resp
    #
    # Each 'rpc' adds an RpcDesc to classes including this module, and
    # #assert_rpc_descs_have_methods is used to ensure the including class
    # provides methods with signatures that support all the descriptors.
    module Dsl
      # This configures the method names that the serializable message
      # implementation uses to marshal and unmarshal messages.
      #
      # - unmarshal_class method must be a class method on the serializable
      # message type that takes a string (byte stream) and produces and object
      #
      # - marshal_class_method is called on a serializable message instance
      # and produces a serialized string.
      #
      # The Dsl verifies that the types in the descriptor have both the
      # unmarshal and marshal methods.
      attr_writer(:marshal_class_method, :unmarshal_class_method)

      # This allows configuration of the service name.
      attr_accessor(:service_name)

      # Adds an RPC spec.
      #
      # Takes the RPC name and the classes representing the types to be
      # serialized, and adds them to the including classes rpc_desc hash.
      #
      # input and output should both have the methods #marshal and #unmarshal
      # that are responsible for writing and reading an object instance from a
      # byte buffer respectively.
      #
      # @param name [String] the name of the rpc
      # @param input [Object] the input parameter's class
      # @param output [Object] the output parameter's class
      def rpc(name, input, output)
        fail(DuplicateRpcName, name) if rpc_descs.key? name
        assert_can_marshal(input)
        assert_can_marshal(output)
        rpc_descs[name] = RpcDesc.new(name, input, output,
                                      marshal_class_method,
                                      unmarshal_class_method)
      end

      def inherited(subclass)
        # Each subclass should have a distinct class variable with its own
        # rpc_descs
        subclass.rpc_descs.merge!(rpc_descs)
        subclass.service_name = service_name
      end

      # the name of the instance method used to marshal events to a byte
      # stream.
      def marshal_class_method
        @marshal_class_method ||= :marshal
      end

      # the name of the class method used to unmarshal from a byte stream.
      def unmarshal_class_method
        @unmarshal_class_method ||= :unmarshal
      end

      def assert_can_marshal(cls)
        cls = cls.type if cls.is_a? RpcDesc::Stream
        mth = unmarshal_class_method
        unless cls.methods.include? mth
          fail(ArgumentError, "#{cls} needs #{cls}.#{mth}")
        end
        mth = marshal_class_method
        return if cls.methods.include? mth
        fail(ArgumentError, "#{cls} needs #{cls}.#{mth}")
      end

      # @param cls [Class] the class of a serializable type
      # @return cls wrapped in a RpcDesc::Stream
      def stream(cls)
        assert_can_marshal(cls)
        RpcDesc::Stream.new(cls)
      end

      # the RpcDescs defined for this GenericService, keyed by name.
      def rpc_descs
        @rpc_descs ||= {}
      end

      # Creates a rpc client class with methods for accessing the methods
      # currently in rpc_descs.
      def rpc_stub_class
        descs = rpc_descs
        route_prefix = service_name
        Class.new(ClientStub) do
          # @param host [String] the host the stub connects to
          # @param creds [Core::ChannelCredentials|Symbol] The channel
          #     credentials to use, or :this_channel_is_insecure otherwise
          # @param kw [KeywordArgs] the channel arguments, plus any optional
          #                         args for configuring the client's channel
          def initialize(host, creds, **kw)
            super(host, Core::CompletionQueue.new, creds, **kw)
          end

          # Used define_method to add a method for each rpc_desc.  Each method
          # calls the base class method for the given descriptor.
          descs.each_pair do |name, desc|
            mth_name = GenericService.underscore(name.to_s).to_sym
            marshal = desc.marshal_proc
            unmarshal = desc.unmarshal_proc(:output)
            route = "/#{route_prefix}/#{name}"
            if desc.request_response?
              define_method(mth_name) do |req, **kw|
                GRPC.logger.debug("calling #{@host}:#{route}")
                request_response(route, req, marshal, unmarshal, **kw)
              end
            elsif desc.client_streamer?
              define_method(mth_name) do |reqs, **kw|
                GRPC.logger.debug("calling #{@host}:#{route}")
                client_streamer(route, reqs, marshal, unmarshal, **kw)
              end
            elsif desc.server_streamer?
              define_method(mth_name) do |req, **kw, &blk|
                GRPC.logger.debug("calling #{@host}:#{route}")
                server_streamer(route, req, marshal, unmarshal, **kw, &blk)
              end
            else  # is a bidi_stream
              define_method(mth_name) do |reqs, **kw, &blk|
                GRPC.logger.debug("calling #{@host}:#{route}")
                bidi_streamer(route, reqs, marshal, unmarshal, **kw, &blk)
              end
            end
          end
        end
      end

      # Asserts that the appropriate methods are defined for each added rpc
      # spec. Is intended to aid verifying that server classes are correctly
      # implemented.
      def assert_rpc_descs_have_methods
        rpc_descs.each_pair do |m, spec|
          mth_name = GenericService.underscore(m.to_s).to_sym
          unless instance_methods.include?(mth_name)
            fail "#{self} does not provide instance method '#{mth_name}'"
          end
          spec.assert_arity_matches(instance_method(mth_name))
        end
      end
    end

    def self.included(o)
      o.extend(Dsl)
      # Update to the use the service name including module. Provide a default
      # that can be nil e.g. when modules are declared dynamically.
      return unless o.service_name.nil?
      if o.name.nil?
        o.service_name = 'GenericService'
      else
        modules = o.name.split('::')
        if modules.length > 2
          o.service_name = modules[modules.length - 2]
        else
          o.service_name = modules.first
        end
      end
    end
  end
end
