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
    # Composes the current credential object with one or more CallCredentials.
    # This method is designed to be included in both ChannelCredentials and
    # CompositeChannelCredentials for polymorphic behavior.
    module ComposableCredentials
      def compose(*new_call_credentials)
        new_call_credentials.each do |cred|
          unless cred.is_a?(GRPC::Core::CallCredentials)
            raise TypeError, 'composed credential must be a GRPC::Core::CallCredentials object'
          end
        end

        base = is_a?(CompositeChannelCredentials) ? channel_credentials : self
        existing = is_a?(CompositeChannelCredentials) ? call_credentials : []

        CompositeChannelCredentials.new(base, existing + new_call_credentials)
      end
    end

    class CompositeChannelCredentials
      include ComposableCredentials
      attr_reader :channel_credentials, :call_credentials

      def initialize(channel_credentials, call_credentials)
        @channel_credentials = channel_credentials
        @call_credentials = call_credentials
      end
    end

    class ChannelCredentials
      include ComposableCredentials
    end
  end
end