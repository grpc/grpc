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

# GRPC contains the General RPC module.
module GRPC
  ##
  # Represents a registry of added interceptors available for enumeration.
  # The registry can be used for both server and client interceptors.
  # This class is internal to gRPC and not meant for public usage.
  #
  class InterceptorRegistry
    ##
    # An error raised when an interceptor is attempted to be added
    # that does not extend GRPC::Interceptor
    #
    class DescendantError < StandardError; end

    ##
    # Initialize the registry with an empty interceptor list
    # This is an EXPERIMENTAL API.
    #
    def initialize(interceptors = [])
      @interceptors = []
      interceptors.each do |i|
        base = GRPC::Interceptor
        unless i.class.ancestors.include?(base)
          fail DescendantError, "Interceptors must descend from #{base}"
        end
        @interceptors << i
      end
    end

    ##
    # Builds an interception context from this registry
    #
    # @return [InterceptionContext]
    #
    def build_context
      InterceptionContext.new(@interceptors)
    end
  end
end
