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
    class ChannelCredentials
      def compose(*others)
        return self if others.empty?
        others.each do |o|
          unless o.is_a?(CallCredentials) || o.is_a?(CompositeCallCredentials)
            fail TypeError, 'Argument to compose must be a CallCredentials'
          end
        end
        if others.size == 1
          CompositeChannelCredentials.new(self, others.first)
        else
          CompositeChannelCredentials.new(self, CompositeCallCredentials.new(others))
        end
      end
    end

    class XdsChannelCredentials
      def compose(*others)
        return self if others.empty?
        others.each do |o|
          unless o.is_a?(CallCredentials) || o.is_a?(CompositeCallCredentials)
            fail TypeError, 'Argument to compose must be a CallCredentials'
          end
        end
        if others.size == 1
          CompositeChannelCredentials.new(self, others.first)
        else
          CompositeChannelCredentials.new(self, CompositeCallCredentials.new(others))
        end
      end
    end

    class CompositeChannelCredentials
      attr_reader :channel_credentials, :call_credentials

      def initialize(channel_creds, call_creds)
        @channel_credentials = channel_creds
        @call_credentials = call_creds
      end

      def compose(*others)
        return self if others.empty?
        others.each do |o|
          unless o.is_a?(CallCredentials) || o.is_a?(CompositeCallCredentials)
            fail TypeError, 'Argument to compose must be a CallCredentials'
          end
        end
        if @call_credentials
          CompositeChannelCredentials.new(@channel_credentials, @call_credentials.compose(*others))
        else
          CompositeChannelCredentials.new(@channel_credentials, CompositeCallCredentials.new(others))
        end
      end
    end
  end
end
