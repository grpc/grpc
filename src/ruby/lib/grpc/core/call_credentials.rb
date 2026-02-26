# Copyright 2025 The gRPC Authors
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

module GRPC
  module Core
    # Provides per-call authentication by accepting a proc that generates
    # authentication metadata for each RPC.
    class CallCredentials
      def initialize(auth_proc)
        fail(TypeError, 'Argument to CallCredentials#new must be a proc') unless auth_proc.is_a? Proc
        @auth_proc = auth_proc
      end

      def get_metadata(service_url: nil, method_name: nil)
        @auth_proc.call(jwt_aud_uri: service_url)
      end

      def compose(*other_call_credentials)
        other_call_credentials.each do |cred|
          unless cred.is_a?(GRPC::Core::CallCredentials)
            fail TypeError, 'can only compose with CallCredentials'
          end
        end
        CompositeCallCredentials.new([self] + other_call_credentials)
      end
    end

    # Combines multiple CallCredentials instances into one.
    class CompositeCallCredentials < CallCredentials
      attr_reader :call_credentials

      def initialize(call_credentials_list)
        @call_credentials = call_credentials_list
      end

      def get_metadata(service_url: nil, method_name: nil)
        result = {}
        @call_credentials.each do |cred|
          metadata = cred.get_metadata(service_url: service_url, method_name: method_name)
          result.merge!(metadata) if metadata
        end
        result
      end

      def compose(*other_call_credentials)
        other_call_credentials.each do |cred|
          unless cred.is_a?(GRPC::Core::CallCredentials)
            fail TypeError, 'can only compose with CallCredentials'
          end
        end
        CompositeCallCredentials.new(@call_credentials + other_call_credentials)
      end
    end
  end
end