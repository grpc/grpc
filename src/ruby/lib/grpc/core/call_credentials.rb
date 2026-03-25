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
    # CallCredentials represents per-call credentials.
    class CallCredentials
      attr_reader :auth_proc

      def initialize(auth_proc = nil, &block)
        @auth_proc = auth_proc || block
        fail TypeError, 'Argument to CallCredentials#new must be a proc' unless @auth_proc.is_a?(Proc)
      end

      def get_metadata(context)
        @auth_proc.call(context)
      end

      def compose(*others)
        return self if others.empty?
        others.each do |o|
          unless o.is_a?(CallCredentials) || o.is_a?(CompositeCallCredentials)
            fail TypeError, 'Argument to compose must be a CallCredentials'
          end
        end
        CompositeCallCredentials.new([self] + others)
      end
    end

    class CompositeCallCredentials < CallCredentials
      # CompositeCallCredentials doesn't call super because it manages
      # an array of credentials rather than a single auth_proc.
      # The inheritance is primarily for type checking in compose methods.
      # rubocop:disable Lint/MissingSuper
      def initialize(*creds)
        @creds = creds.flatten
      end
      # rubocop:enable Lint/MissingSuper

      def get_metadata(context)
        metadata = {}
        @creds.each do |c|
          metadata.merge!(c.get_metadata(context))
        end
        metadata
      end

      def compose(*others)
        return self if others.empty?
        others.each do |o|
          unless o.is_a?(CallCredentials) || o.is_a?(CompositeCallCredentials)
            fail TypeError, 'Argument to compose must be a CallCredentials'
          end
        end
        CompositeCallCredentials.new(@creds + others)
      end
    end

    # Internal utilities for credential management
    # @api private
    module CallCredentialsHelper
      # Resolves credentials from multiple sources
      # @param channel_call_creds [CallCredentials, nil]
      # @param call_credentials [CallCredentials, nil]
      # @return [CallCredentials, nil]
      def self.resolve(channel_call_creds, call_credentials)
        if channel_call_creds && call_credentials
          channel_call_creds.compose(call_credentials)
        else
          channel_call_creds || call_credentials
        end
      end

      # Applies credentials to request metadata
      # @param credentials [CallCredentials, nil]
      # @param metadata [Hash]
      # @param host [String]
      # @param channel_creds [Symbol, ChannelCredentials]
      def self.apply(credentials, metadata, host, channel_creds)
        return unless credentials
        return if channel_creds == :this_channel_is_insecure

        context = { service_url: host, jwt_aud_uri: host }
        begin
          creds_metadata = credentials.get_metadata(context)
          creds_metadata&.each do |k, v|
            metadata[k.to_s] = v.is_a?(Array) ? v.map(&:to_s) : v.to_s
          end
        rescue StandardError => e
          fail GRPC::Unauthenticated, "Call credentials failed: #{e.message}"
        end
      end
    end
  end
end
