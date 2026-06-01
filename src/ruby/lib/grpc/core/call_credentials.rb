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

      def composite?
        false
      end

      def compose(*others)
        return self if others.empty?
        valid_others = validate_credentials_list!(others)
        CompositeCallCredentials.new([self] + valid_others)
      end

      private

      def validate_credentials_list!(list)
        list.flatten.each do |o|
          fail TypeError, "Argument to compose must be a CallCredentials, got #{o.class}" \
            unless o.is_a?(CallCredentials)
        end
      end
    end

    class CompositeCallCredentials < CallCredentials
      def initialize(*creds)
        flat_creds = creds.flatten.flat_map do |c|
          c.composite? ? c.creds : c
        end
        @creds = flat_creds.uniq
        super(proc { |context| get_metadata(context) })
      end

      def composite?
        true
      end

      def get_metadata(context)
        @creds.each_with_object({}) do |c, metadata|
          creds_metadata = c.get_metadata(context)
          next unless creds_metadata
          metadata.merge!(
            creds_metadata.is_a?(Hash) ? creds_metadata : fail(TypeError, "Call credentials must return Hash or nil, got #{creds_metadata.class}")
          )
        end
      end

      def compose(*others)
        return self if others.empty?
        valid_others = validate_credentials_list!(others)
        CompositeCallCredentials.new(@creds + valid_others)
      end

      protected

      attr_reader :creds
    end
  end
end
