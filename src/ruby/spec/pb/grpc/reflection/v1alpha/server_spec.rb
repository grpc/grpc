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

# frozen_string_literal: true

require 'grpc'
require 'grpc/health/v1/health_pb'
require 'grpc/reflection/v1alpha/server'
require 'google/protobuf/descriptor_pb'

# Inline proto2 fixture with extensions for testing extension lookups.
ext_test_fd = Google::Protobuf::FileDescriptorProto.new(
  name: 'test/ext_test.proto',
  package: 'test.ext',
  syntax: 'proto2',
  message_type: [
    Google::Protobuf::DescriptorProto.new(
      name: 'MessageWithExtensions',
      extension_range: [
        Google::Protobuf::DescriptorProto::ExtensionRange.new(start: 100, end: 1000)
      ]
    )
  ],
  extension: [
    Google::Protobuf::FieldDescriptorProto.new(
      name: 'field_a', number: 124, type: :TYPE_INT64,
      label: :LABEL_OPTIONAL, extendee: '.test.ext.MessageWithExtensions'
    ),
    Google::Protobuf::FieldDescriptorProto.new(
      name: 'field_b', number: 125, type: :TYPE_STRING,
      label: :LABEL_OPTIONAL, extendee: '.test.ext.MessageWithExtensions'
    )
  ],
  service: [
    Google::Protobuf::ServiceDescriptorProto.new(
      name: 'ExtTestService',
      method: [Google::Protobuf::MethodDescriptorProto.new(
        name: 'Noop',
        input_type: '.test.ext.MessageWithExtensions',
        output_type: '.test.ext.MessageWithExtensions'
      )]
    )
  ]
)
Google::Protobuf::DescriptorPool.generated_pool.add_serialized_file(
  Google::Protobuf::FileDescriptorProto.encode(ext_test_fd)
)

RSpec.describe Grpc::Reflection::V1alpha::Server do
  subject(:reflection) { described_class.new(service_names) }

  let(:service_names) do
    %w[
      grpc.health.v1.Health
      grpc.reflection.v1alpha.ServerReflection
    ]
  end

  def send_request(**fields)
    req = Grpc::Reflection::V1alpha::ServerReflectionRequest.new(**fields)
    reflection.server_reflection_info([req], nil).first
  end

  describe '#server_reflection_info' do
    context 'when listing services' do
      it 'returns all registered service names in sorted order' do
        resp = send_request(list_services: '')
        names = resp.list_services_response.service.map(&:name)
        expect(names).to match_array(service_names)
        expect(names).to eq(names.sort)
      end
    end

    context 'when fetching file by filename' do
      it 'returns the file descriptor for a known file' do
        resp = send_request(file_by_filename: 'grpc/health/v1/health.proto')
        expect(resp.file_descriptor_response.file_descriptor_proto).not_to be_empty
      end

      it 'returns NOT_FOUND for an unknown file' do
        resp = send_request(file_by_filename: 'no_such.proto')
        expect(resp.error_response.error_code).to eq(GRPC::Core::StatusCodes::NOT_FOUND)
      end
    end

    context 'when fetching file containing symbol' do
      it 'finds file by service name' do
        resp = send_request(file_containing_symbol: 'grpc.health.v1.Health')
        expect(resp.file_descriptor_response.file_descriptor_proto).not_to be_empty
      end

      it 'returns NOT_FOUND for an unknown symbol' do
        resp = send_request(file_containing_symbol: 'no.such.Symbol')
        expect(resp.error_response.error_code).to eq(GRPC::Core::StatusCodes::NOT_FOUND)
      end
    end

    context 'when fetching file containing extension' do
      it 'returns NOT_FOUND for an unknown extension' do
        ext_req = Grpc::Reflection::V1alpha::ExtensionRequest.new(
          containing_type: 'no.such.Type',
          extension_number: 1
        )
        resp = send_request(file_containing_extension: ext_req)
        expect(resp.error_response.error_code).to eq(GRPC::Core::StatusCodes::NOT_FOUND)
      end

      context 'with proto2 extensions registered' do
        subject(:reflection) { described_class.new(%w[test.ext.ExtTestService]) }

        it 'returns the file descriptor for a known extension' do
          ext_req = Grpc::Reflection::V1alpha::ExtensionRequest.new(
            containing_type: 'test.ext.MessageWithExtensions',
            extension_number: 124
          )
          resp = send_request(file_containing_extension: ext_req)
          expect(resp.file_descriptor_response.file_descriptor_proto).not_to be_empty
        end
      end
    end

    context 'when fetching all extension numbers of type' do
      it 'returns NOT_FOUND for an unknown type' do
        resp = send_request(all_extension_numbers_of_type: 'no.such.Type')
        expect(resp.error_response.error_code).to eq(GRPC::Core::StatusCodes::NOT_FOUND)
      end

      context 'with proto2 extensions registered' do
        subject(:reflection) { described_class.new(%w[test.ext.ExtTestService]) }

        it 'returns extension numbers for a known type' do
          resp = send_request(all_extension_numbers_of_type: 'test.ext.MessageWithExtensions')
          numbers = resp.all_extension_numbers_response.extension_number.sort
          expect(numbers).to eq([124, 125])
        end
      end
    end

    context 'when an invalid request is sent' do
      it 'returns INVALID_ARGUMENT' do
        req  = Grpc::Reflection::V1alpha::ServerReflectionRequest.new
        resp = reflection.server_reflection_info([req], nil).first
        expect(resp.error_response.error_code).to eq(GRPC::Core::StatusCodes::INVALID_ARGUMENT)
      end
    end

    context 'multi-request streaming' do
      it 'handles multiple requests in one stream' do
        requests = [
          Grpc::Reflection::V1alpha::ServerReflectionRequest.new(list_services: ''),
          Grpc::Reflection::V1alpha::ServerReflectionRequest.new(file_by_filename: 'grpc/health/v1/health.proto')
        ]
        responses = reflection.server_reflection_info(requests, nil).to_a
        expect(responses.size).to eq(2)
        expect(responses[0].list_services_response.service).not_to be_empty
        expect(responses[1].file_descriptor_response.file_descriptor_proto).not_to be_empty
      end
    end
  end

  describe 'SERVICE_NAME' do
    it 'equals the fully-qualified reflection service name' do
      expect(Grpc::Reflection::V1alpha::SERVICE_NAME).to eq('grpc.reflection.v1alpha.ServerReflection')
    end
  end

  describe '.enable_server_reflection' do
    it 'registers the reflection service on the given server' do
      server = double('RpcServer')
      expect(server).to receive(:handle).with(an_instance_of(described_class))
      Grpc::Reflection::V1alpha.enable_server_reflection(%w[test.Service], server)
    end
  end
end
