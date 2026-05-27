# frozen_string_literal: true

# Copyright 2026 gRPC authors.
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
    # Provides shared #compose logic for channel credential classes.
    # @api private
    module ChannelCredentialsComposable
      def compose(*others)
        return self if others.empty?
        flat_others = others.flatten

        flat_others.each do |o|
          fail TypeError, "Argument to compose must be a CallCredentials, got #{o.class}" \
            unless o.is_a?(CallCredentials)
        end

        call_creds = flat_others.size == 1 ? flat_others.first : CompositeCallCredentials.new(flat_others)
        CompositeChannelCredentials.new(self, call_creds)
      end
    end

    class ChannelCredentials
      prepend ChannelCredentialsComposable
    end

    class XdsChannelCredentials
      prepend ChannelCredentialsComposable
    end

    class CompositeChannelCredentials
      attr_reader :channel_credentials, :call_credentials

      def initialize(channel_creds, call_creds)
        @channel_credentials = channel_creds
        @call_credentials = call_creds
      end

      def compose(*others)
        return self if others.empty?
        flat_others = others.flatten

        flat_others.each do |o|
          fail TypeError, "Argument to compose must be a CallCredentials, got #{o.class}" \
            unless o.is_a?(CallCredentials)
        end

        if @call_credentials
          CompositeChannelCredentials.new(@channel_credentials, @call_credentials.compose(*flat_others))
        else
          CompositeChannelCredentials.new(@channel_credentials, CompositeCallCredentials.new(flat_others))
        end
      end
    end
  end
end
