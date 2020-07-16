# Copyright 2017 gRPC authors.
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
require_relative 'interceptor_registry'

# GRPC contains the General RPC module.
module GRPC
  ##
  # Base class for interception in GRPC
  #
  class Interceptor
    ##
    # @param [Hash] options A hash of options that will be used
    #   by the interceptor. This is an EXPERIMENTAL API.
    #
    def initialize(options = {})
      @options = options || {}
    end
  end

  ##
  # ClientInterceptor allows for wrapping outbound gRPC client stub requests.
  # This is an EXPERIMENTAL API.
  #
  class ClientInterceptor < Interceptor
    ##
    # Intercept a unary request response call
    #
    # @param [Object] request
    # @param [GRPC::ActiveCall] call
    # @param [String] method
    # @param [Hash] metadata
    #
    def request_response(request: nil, call: nil, method: nil, metadata: nil)
      GRPC.logger.debug "Intercepting request response method #{method}" \
        " for request #{request} with call #{call} and metadata: #{metadata}"
      yield
    end

    ##
    # Intercept a client streaming call
    #
    # @param [Enumerable] requests
    # @param [GRPC::ActiveCall] call
    # @param [String] method
    # @param [Hash] metadata
    #
    def client_streamer(requests: nil, call: nil, method: nil, metadata: nil)
      GRPC.logger.debug "Intercepting client streamer method #{method}" \
       " for requests #{requests} with call #{call} and metadata: #{metadata}"
      yield
    end

    ##
    # Intercept a server streaming call
    #
    # @param [Object] request
    # @param [GRPC::ActiveCall] call
    # @param [String] method
    # @param [Hash] metadata
    #
    def server_streamer(request: nil, call: nil, method: nil, metadata: nil)
      GRPC.logger.debug "Intercepting server streamer method #{method}" \
        " for request #{request} with call #{call} and metadata: #{metadata}"
      yield
    end

    ##
    # Intercept a BiDi streaming call
    #
    # @param [Enumerable] requests
    # @param [GRPC::ActiveCall] call
    # @param [String] method
    # @param [Hash] metadata
    #
    def bidi_streamer(requests: nil, call: nil, method: nil, metadata: nil)
      GRPC.logger.debug "Intercepting bidi streamer method #{method}" \
        " for requests #{requests} with call #{call} and metadata: #{metadata}"
      yield
    end
  end

  ##
  # ServerInterceptor allows for wrapping gRPC server execution handling.
  # This is an EXPERIMENTAL API.
  #
  class ServerInterceptor < Interceptor
    ##
    # Intercept a unary request response call.
    #
    # @param [Object] request
    # @param [GRPC::ActiveCall::SingleReqView] call
    # @param [Method] method
    #
    def request_response(request: nil, call: nil, method: nil)
      GRPC.logger.debug "Intercepting request response method #{method}" \
        " for request #{request} with call #{call}"
      yield
    end

    ##
    # Intercept a client streaming call
    #
    # @param [GRPC::ActiveCall::MultiReqView] call
    # @param [Method] method
    #
    def client_streamer(call: nil, method: nil)
      GRPC.logger.debug "Intercepting client streamer method #{method}" \
        " with call #{call}"
      yield
    end

    ##
    # Intercept a server streaming call
    #
    # @param [Object] request
    # @param [GRPC::ActiveCall::SingleReqView] call
    # @param [Method] method
    #
    def server_streamer(request: nil, call: nil, method: nil)
      GRPC.logger.debug "Intercepting server streamer method #{method}" \
        " for request #{request} with call #{call}"
      yield
    end

    ##
    # Intercept a BiDi streaming call
    #
    # @param [Enumerable<Object>] requests
    # @param [GRPC::ActiveCall::MultiReqView] call
    # @param [Method] method
    #
    def bidi_streamer(requests: nil, call: nil, method: nil)
      GRPC.logger.debug "Intercepting bidi streamer method #{method}" \
        " for requests #{requests} with call #{call}"
      yield
    end
  end

  ##
  # Represents the context in which an interceptor runs. Used to provide an
  # injectable mechanism for handling interception. This is an EXPERIMENTAL API.
  #
  class InterceptionContext
    ##
    # @param interceptors [Array<GRPC::Interceptor>]
    #
    def initialize(interceptors = [])
      @interceptors = interceptors.dup
    end

    ##
    # Intercept the call and fire out to interceptors in a FIFO execution.
    # This is an EXPERIMENTAL API.
    #
    # @param [Symbol] type The request type
    # @param [Hash] args The arguments for the call
    #
    def intercept!(type, args = {})
      return yield if @interceptors.none?

      i = @interceptors.pop
      return yield unless i

      i.send(type, **args) do
        if @interceptors.any?
          intercept!(type, args) do
            yield
          end
        else
          yield
        end
      end
    end
  end
end
