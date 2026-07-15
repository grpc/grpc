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
    # Utility methods for resolving and applying call credentials to metadata.
    # @api private
    module CallCredentialsHelper
      VALID_HEADER_KEY_PATTERN = /\A[a-z0-9\-_.]+\z/

      # Composes channel and per-call credentials when both present, otherwise
      # returns whichever is available.
      def self.resolve(channel_call_creds, call_credentials)
        return channel_call_creds.compose(call_credentials) if channel_call_creds && call_credentials
        channel_call_creds || call_credentials
      end

      # Returns true if +key+ is a valid gRPC metadata header key.
      def self.valid_header_key?(key)
        !key.nil? && !key.empty? && VALID_HEADER_KEY_PATTERN.match?(key)
      end

      # Parses an RPC method path into [service_url, method_name].
      # e.g. '/echo.EchoServer/Echo' on 'foo.test.google.fr' gives
      # ['https://foo.test.google.fr/echo.EchoServer', 'Echo'].
      def self.parse_method_info(ssl_target, method)
        return ["https://#{ssl_target}", nil] if method.nil? || method.empty?
        last_slash = method.rindex('/')
        service_path = last_slash&.positive? ? method[0, last_slash] : ''
        [
          "https://#{ssl_target}#{service_path}",
          last_slash ? method[(last_slash + 1)..] : nil
        ]
      end

      # Merges +creds_metadata+ into +metadata+, validating all keys.
      # Raises GRPC::Unavailable on invalid type or illegal header keys.
      def self.merge_creds_metadata!(creds_metadata, metadata)
        return if creds_metadata.nil?
        unless creds_metadata.is_a?(Hash)
          fail GRPC::Unavailable, "Call credentials must return Hash or nil, got #{creds_metadata.class}"
        end
        creds_metadata.each do |key, value|
          key_str = key.to_s
          unless valid_header_key?(key_str)
            fail GRPC::Unavailable, "Illegal metadata: '#{key_str}' is an invalid header key"
          end
          metadata[key_str] = value.is_a?(Array) ? value.map(&:to_s) : value.to_s
        end
      end

      # Calls +credentials.get_metadata+ and merges the result into +metadata+.
      # No-op when +credentials+ is nil or the channel is insecure.
      # Raises GRPC::Unavailable on failure.
      def self.apply(credentials, metadata, ssl_target, channel_creds, method = nil)
        return unless credentials
        return if channel_creds == :this_channel_is_insecure
        service_url, method_name = parse_method_info(ssl_target, method)
        context = { service_url: service_url, jwt_aud_uri: service_url, method_name: method_name }
        begin
          merge_creds_metadata!(credentials.get_metadata(context), metadata)
        rescue GRPC::BadStatus
          raise
        rescue StandardError => e
          fail GRPC::Unavailable, "Call credentials failed: #{e.message}"
        end
      end
    end

    # Prepended into ClientStub when the pure Ruby credentials path is active.
    # Handles CompositeChannelCredentials splitting and applies credentials via
    # metadata injection instead of the C extension set_credentials! path.
    # @api private
    module CompositeCredentialsHandler
      def initialize(host, creds,
                     channel_override: nil,
                     timeout: nil,
                     propagate_mask: nil,
                     channel_args: {},
                     interceptors: [])
        if creds.is_a?(Core::CompositeChannelCredentials)
          pure_call_creds = creds.call_credentials
          creds = creds.channel_credentials
          super(host, creds,
                channel_override: channel_override,
                timeout: timeout,
                propagate_mask: propagate_mask,
                channel_args: channel_args,
                interceptors: interceptors)
          @call_creds = pure_call_creds
        else
          super
        end
      end

      # credentials is intentionally overridden to nil in super to skip set_credentials!
      def new_active_call(method, marshal, unmarshal,
                          deadline: nil,
                          parent: nil,
                          credentials: nil) # rubocop:disable Lint/UnusedMethodArgument
        super(method, marshal, unmarshal,
              deadline: deadline,
              parent: parent,
              credentials: nil)
      end

      private

      def resolve_call_metadata(metadata, credentials, method)
        resolved = CallCredentialsHelper.resolve(@call_creds, credentials)
        metadata.dup.tap { |m| CallCredentialsHelper.apply(resolved, m, @host, @channel_creds, method) }
      end
    end
  end
end
