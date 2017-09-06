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

require_relative 'client_stub'
require_relative 'rpc_desc'

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
        define_method(GenericService.underscore(name.to_s).to_sym) do |_, _|
          fail GRPC::BadStatus.new_status_exception(
            GRPC::Core::StatusCodes::UNIMPLEMENTED)
        end
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
            super(host, creds, **kw)
          end

          # Used define_method to add a method for each rpc_desc.  Each method
          # calls the base class method for the given descriptor.
          descs.each_pair do |name, desc|
            mth_name = GenericService.underscore(name.to_s).to_sym
            marshal = desc.marshal_proc
            unmarshal = desc.unmarshal_proc(:output)
            route = "/#{route_prefix}/#{name}"
            if desc.request_response?
              define_method(mth_name) do |req, metadata = {}|
                GRPC.logger.debug("calling #{@host}:#{route}")
                request_response(route, req, marshal, unmarshal, metadata)
              end
            elsif desc.client_streamer?
              define_method(mth_name) do |reqs, metadata = {}|
                GRPC.logger.debug("calling #{@host}:#{route}")
                client_streamer(route, reqs, marshal, unmarshal, metadata)
              end
            elsif desc.server_streamer?
              define_method(mth_name) do |req, metadata = {}, &blk|
                GRPC.logger.debug("calling #{@host}:#{route}")
                server_streamer(route, req, marshal, unmarshal, metadata, &blk)
              end
            else  # is a bidi_stream
              define_method(mth_name) do |reqs, metadata = {}, &blk|
                GRPC.logger.debug("calling #{@host}:#{route}")
                bidi_streamer(route, reqs, marshal, unmarshal, metadata, &blk)
              end
            end
          end
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
