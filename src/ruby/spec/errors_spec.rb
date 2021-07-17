# Copyright 2015 gRPC authors.
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

require 'spec_helper'
require 'google/protobuf/well_known_types'
require 'google/rpc/status_pb'
require_relative '../pb/src/proto/grpc/testing/messages_pb'

describe GRPC::BadStatus do
  describe :attributes do
    it 'has attributes' do
      code = 1
      details = 'details'
      metadata = { 'key' => 'val' }

      exception = GRPC::BadStatus.new(code, details, metadata)

      expect(exception.code).to eq code
      expect(exception.details).to eq details
      expect(exception.metadata).to eq metadata
    end
  end

  describe :new_status_exception do
    let(:codes_and_classes) do
      [
        [GRPC::Core::StatusCodes::OK, GRPC::Ok],
        [GRPC::Core::StatusCodes::CANCELLED, GRPC::Cancelled],
        [GRPC::Core::StatusCodes::UNKNOWN, GRPC::Unknown],
        [GRPC::Core::StatusCodes::INVALID_ARGUMENT, GRPC::InvalidArgument],
        [GRPC::Core::StatusCodes::DEADLINE_EXCEEDED, GRPC::DeadlineExceeded],
        [GRPC::Core::StatusCodes::NOT_FOUND, GRPC::NotFound],
        [GRPC::Core::StatusCodes::ALREADY_EXISTS, GRPC::AlreadyExists],
        [GRPC::Core::StatusCodes::PERMISSION_DENIED, GRPC::PermissionDenied],
        [GRPC::Core::StatusCodes::UNAUTHENTICATED, GRPC::Unauthenticated],
        [GRPC::Core::StatusCodes::RESOURCE_EXHAUSTED, GRPC::ResourceExhausted],
        [GRPC::Core::StatusCodes::FAILED_PRECONDITION, GRPC::FailedPrecondition],
        [GRPC::Core::StatusCodes::ABORTED, GRPC::Aborted],
        [GRPC::Core::StatusCodes::OUT_OF_RANGE, GRPC::OutOfRange],
        [GRPC::Core::StatusCodes::UNIMPLEMENTED, GRPC::Unimplemented],
        [GRPC::Core::StatusCodes::INTERNAL, GRPC::Internal],
        [GRPC::Core::StatusCodes::UNAVAILABLE, GRPC::Unavailable],
        [GRPC::Core::StatusCodes::DATA_LOSS, GRPC::DataLoss],
        [99, GRPC::BadStatus] # Unknown codes default to BadStatus
      ]
    end

    it 'maps codes to the correct error class' do
      codes_and_classes.each do |code, grpc_error_class|
        exception = GRPC::BadStatus.new_status_exception(code)

        expect(exception).to be_a grpc_error_class
      end
    end
  end

  describe :to_status do
    it 'gets status' do
      code = 1
      details = 'details'
      metadata = { 'key' => 'val' }

      exception = GRPC::BadStatus.new(code, details, metadata)
      status = Struct::Status.new(code, details, metadata)

      expect(exception.to_status).to eq status
    end
  end

  describe :to_rpc_status do
    let(:simple_request_any) do
      Google::Protobuf::Any.new.tap do |any|
        any.pack(
          Grpc::Testing::SimpleRequest.new(
            payload: Grpc::Testing::Payload.new(body: 'request')
          )
        )
      end
    end
    let(:simple_response_any) do
      Google::Protobuf::Any.new.tap do |any|
        any.pack(
          Grpc::Testing::SimpleResponse.new(
            payload: Grpc::Testing::Payload.new(body: 'response')
          )
        )
      end
    end
    let(:payload_any) do
      Google::Protobuf::Any.new.tap do |any|
        any.pack(Grpc::Testing::Payload.new(body: 'payload'))
      end
    end

    it 'decodes proto values' do
      rpc_status = Google::Rpc::Status.new(
        code: 1,
        message: 'matching message',
        details: [simple_request_any, simple_response_any, payload_any]
      )
      rpc_status_proto = Google::Rpc::Status.encode(rpc_status)

      code = 1
      details = 'details'
      metadata = { 'grpc-status-details-bin' => rpc_status_proto }

      exception = GRPC::BadStatus.new(code, details, metadata)

      expect(exception.to_rpc_status).to eq rpc_status
    end

    it 'does not raise when decoding a bad proto' do
      code = 1
      details = 'details'
      metadata = { 'grpc-status-details-bin' => 'notavalidprotostream' }

      exception = GRPC::BadStatus.new(code, details, metadata)

      expect(exception.to_rpc_status).to be nil

      error_msg = 'parse error: to_rpc_status failed'
      error_desc = '<Google::Protobuf::ParseError> ' \
        'Error occurred during parsing'

      # Check that the parse error was logged correctly
      log_contents = @log_output.read
      expect(log_contents).to include "WARN  GRPC : #{error_msg}"
      expect(log_contents).to include "WARN  GRPC : #{error_desc}"
    end
  end
end
