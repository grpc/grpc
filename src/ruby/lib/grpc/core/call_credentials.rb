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
      # Valid header key pattern per gRPC spec: lowercase letters, digits, hyphen, underscore, dot
      VALID_HEADER_KEY_PATTERN = /\A[a-z0-9\-_.]+\z/

      # Resolves credentials from multiple sources
      def self.resolve(channel_call_creds, call_credentials)
        return channel_call_creds.compose(call_credentials) if channel_call_creds && call_credentials

        channel_call_creds || call_credentials
      end

      # Validates that a metadata key is legal per gRPC spec
      def self.valid_header_key?(key)
        !key.nil? && !key.empty? && VALID_HEADER_KEY_PATTERN.match?(key)
      end

      # Builds service URL in format: https://host/service (matches C core)
      # @param ssl_target [String] SSL target host (e.g., 'foo.test.google.fr')
      # @param method [String, nil] RPC method path (e.g., '/echo.EchoServer/Echo')
      # @return [String] service URL (e.g., 'https://foo.test.google.fr/echo.EchoServer')
      def self.build_service_url(ssl_target, method)
        return "https://#{ssl_target}" if method.nil? || method.empty?

        # Method format: /service.Name/MethodName - extract up to last slash
        last_slash = method.rindex('/')
        service_path = last_slash&.positive? ? method[0, last_slash] : ''
        "https://#{ssl_target}#{service_path}"
      end

      # Extracts method name from method path
      # @param method [String, nil] RPC method path (e.g., '/echo.EchoServer/Echo')
      # @return [String, nil] method name (e.g., 'Echo')
      def self.extract_method_name(method)
        return nil if method.nil? || method.empty?

        last_slash = method.rindex('/')
        last_slash ? method[(last_slash + 1)..] : nil
      end

      # Validates and merges credential metadata into request metadata
      def self.merge_creds_metadata!(creds_metadata, metadata)
        return if creds_metadata.nil?

        unless creds_metadata.is_a?(Hash)
          fail GRPC::Unavailable, "Call credentials must return Hash or nil, got #{creds_metadata.class}"
        end

        creds_metadata.each do |k, v|
          key_str = k.to_s
          unless valid_header_key?(key_str)
            fail GRPC::Unavailable, "Illegal metadata: '#{key_str}' is an invalid header key"
          end
          metadata[key_str] = v.is_a?(Array) ? v.map(&:to_s) : v.to_s
        end
      end

      # Applies credentials to request metadata
      def self.apply(credentials, metadata, ssl_target, channel_creds, method = nil)
        return unless credentials
        return if channel_creds == :this_channel_is_insecure

        service_url = build_service_url(ssl_target, method)
        method_name = extract_method_name(method)
        context = { service_url: service_url, jwt_aud_uri: service_url, method_name: method_name }

        begin
          creds_metadata = credentials.get_metadata(context)
          merge_creds_metadata!(creds_metadata, metadata)
        rescue GRPC::BadStatus
          raise
        rescue StandardError => e
          fail GRPC::Unavailable, "Call credentials failed: #{e.message}"
        end
      end
    end
  end
end
