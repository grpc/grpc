# Copyright 2017 gRPC authors.
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

require 'grpc'
require_relative '../lib/grpc/google_rpc_status_utils'
require_relative '../pb/src/proto/grpc/testing/messages_pb'
require_relative '../pb/src/proto/grpc/testing/messages_pb'
require 'google/protobuf/well_known_types'

include GRPC::Core
include GRPC::Spec::Helpers

describe 'conversion from a status struct to a google protobuf status' do
  it 'fails if the input is not a status struct' do
    begin
      GRPC::GoogleRpcStatusUtils.extract_google_rpc_status('string')
    rescue => e
      exception = e
    end
    expect(exception.is_a?(ArgumentError)).to be true
    expect(exception.message.include?('bad type')).to be true
  end

  it 'returns nil if the header key is missing' do
    status = Struct::Status.new(1, 'details', key: 'val')
    expect(status.metadata.nil?).to be false
    expect(GRPC::GoogleRpcStatusUtils.extract_google_rpc_status(
             status)).to be(nil)
  end

  it 'fails with some error if the header key fails to deserialize' do
    status = Struct::Status.new(1, 'details',
                                'grpc-status-details-bin' => 'string_val')
    expect do
      GRPC::GoogleRpcStatusUtils.extract_google_rpc_status(status)
    end.to raise_error(StandardError)
  end

  it 'silently ignores erroneous mismatch between messages in '\
    'status struct and protobuf status' do
    proto = Google::Rpc::Status.new(code: 1, message: 'proto message')
    encoded_proto = Google::Rpc::Status.encode(proto)
    status = Struct::Status.new(1, 'struct message',
                                'grpc-status-details-bin' => encoded_proto)
    rpc_status = GRPC::GoogleRpcStatusUtils.extract_google_rpc_status(status)
    expect(rpc_status).to eq(proto)
  end

  it 'silently ignores erroneous mismatch between codes in status struct '\
    'and protobuf status' do
    proto = Google::Rpc::Status.new(code: 1, message: 'matching message')
    encoded_proto = Google::Rpc::Status.encode(proto)
    status = Struct::Status.new(2, 'matching message',
                                'grpc-status-details-bin' => encoded_proto)
    rpc_status = GRPC::GoogleRpcStatusUtils.extract_google_rpc_status(status)
    expect(rpc_status).to eq(proto)
  end

  it 'can succesfully convert a status struct into a google protobuf status '\
    'when there are no rpcstatus details' do
    proto = Google::Rpc::Status.new(code: 1, message: 'matching message')
    encoded_proto = Google::Rpc::Status.encode(proto)
    status = Struct::Status.new(1, 'matching message',
                                'grpc-status-details-bin' => encoded_proto)
    out = GRPC::GoogleRpcStatusUtils.extract_google_rpc_status(status)
    expect(out.code).to eq(1)
    expect(out.message).to eq('matching message')
    expect(out.details).to eq([])
  end

  it 'can succesfully convert a status struct into a google protobuf '\
    'status when there are multiple rpcstatus details' do
    simple_request_any = Google::Protobuf::Any.new
    simple_request = Grpc::Testing::SimpleRequest.new(
      payload: Grpc::Testing::Payload.new(body: 'request'))
    simple_request_any.pack(simple_request)
    simple_response_any = Google::Protobuf::Any.new
    simple_response = Grpc::Testing::SimpleResponse.new(
      payload: Grpc::Testing::Payload.new(body: 'response'))
    simple_response_any.pack(simple_response)
    payload_any = Google::Protobuf::Any.new
    payload = Grpc::Testing::Payload.new(body: 'payload')
    payload_any.pack(payload)
    proto = Google::Rpc::Status.new(code: 1,
                                    message: 'matching message',
                                    details: [
                                      simple_request_any,
                                      simple_response_any,
                                      payload_any
                                    ])
    encoded_proto = Google::Rpc::Status.encode(proto)
    status = Struct::Status.new(1, 'matching message',
                                'grpc-status-details-bin' => encoded_proto)
    out = GRPC::GoogleRpcStatusUtils.extract_google_rpc_status(status)
    expect(out.code).to eq(1)
    expect(out.message).to eq('matching message')
    expect(out.details[0].unpack(
             Grpc::Testing::SimpleRequest)).to eq(simple_request)
    expect(out.details[1].unpack(
             Grpc::Testing::SimpleResponse)).to eq(simple_response)
    expect(out.details[2].unpack(
             Grpc::Testing::Payload)).to eq(payload)
  end
end

# Test message
class EchoMsg
  def self.marshal(_o)
    ''
  end

  def self.unmarshal(_o)
    EchoMsg.new
  end
end

# A test service that fills in the "reserved" grpc-status-details-bin trailer,
# for client-side testing of GoogleRpcStatus protobuf extraction from trailers.
class GoogleRpcStatusTestService
  include GRPC::GenericService
  rpc :an_rpc, EchoMsg, EchoMsg

  def initialize(encoded_rpc_status)
    @encoded_rpc_status = encoded_rpc_status
  end

  def an_rpc(_, _)
    # TODO: create a server-side utility API for sending a google rpc status.
    # Applications are not expected to set the grpc-status-details-bin
    # ("grpc"-fixed and reserved for library use) manually.
    # Doing so here is only for testing of the client-side api for extracting
    # a google rpc status, which is useful
    # when the interacting with a server that does fill in this trailer.
    fail GRPC::Unknown.new('test message',
                           'grpc-status-details-bin' => @encoded_rpc_status)
  end
end

GoogleRpcStatusTestStub = GoogleRpcStatusTestService.rpc_stub_class

describe 'receving a google rpc status from a remote endpoint' do
  def start_server(encoded_rpc_status)
    @srv = new_rpc_server_for_testing(pool_size: 1)
    @server_port = @srv.add_http2_port('localhost:0',
                                       :this_port_is_insecure)
    @srv.handle(GoogleRpcStatusTestService.new(encoded_rpc_status))
    @server_thd = Thread.new { @srv.run }
    @srv.wait_till_running
  end

  def stop_server
    expect(@srv.stopped?).to be(false)
    @srv.stop
    @server_thd.join
    expect(@srv.stopped?).to be(true)
  end

  before(:each) do
    simple_request_any = Google::Protobuf::Any.new
    simple_request = Grpc::Testing::SimpleRequest.new(
      payload: Grpc::Testing::Payload.new(body: 'request'))
    simple_request_any.pack(simple_request)
    simple_response_any = Google::Protobuf::Any.new
    simple_response = Grpc::Testing::SimpleResponse.new(
      payload: Grpc::Testing::Payload.new(body: 'response'))
    simple_response_any.pack(simple_response)
    payload_any = Google::Protobuf::Any.new
    payload = Grpc::Testing::Payload.new(body: 'payload')
    payload_any.pack(payload)
    @expected_proto = Google::Rpc::Status.new(
      code: StatusCodes::UNKNOWN,
      message: 'test message',
      details: [simple_request_any, simple_response_any, payload_any])
    start_server(Google::Rpc::Status.encode(@expected_proto))
  end

  after(:each) do
    stop_server
  end

  it 'should receive be able to extract a google rpc status from the '\
    'status struct taken from a BadStatus exception' do
    stub = GoogleRpcStatusTestStub.new("localhost:#{@server_port}",
                                       :this_channel_is_insecure)
    begin
      stub.an_rpc(EchoMsg.new)
    rescue GRPC::BadStatus => e
      rpc_status = GRPC::GoogleRpcStatusUtils.extract_google_rpc_status(
        e.to_status)
    end
    expect(rpc_status).to eq(@expected_proto)
  end

  it 'should receive be able to extract a google rpc status from the '\
    'status struct taken from the op view of a call' do
    stub = GoogleRpcStatusTestStub.new("localhost:#{@server_port}",
                                       :this_channel_is_insecure)
    op = stub.an_rpc(EchoMsg.new, return_op: true)
    begin
      op.execute
    rescue GRPC::BadStatus => e
      status_from_exception = e.to_status
    end
    rpc_status = GRPC::GoogleRpcStatusUtils.extract_google_rpc_status(
      op.status)
    expect(rpc_status).to eq(@expected_proto)
    # "to_status" on the bad status should give the same result
    # as "status" on the "op view".
    expect(GRPC::GoogleRpcStatusUtils.extract_google_rpc_status(
             status_from_exception)).to eq(rpc_status)
  end
end

# A test service that fails without explicitly setting the
# grpc-status-details-bin trailer. Tests assumptions about value
# of grpc-status-details-bin on the client side when the trailer wasn't
# set explicitly.
class NoStatusDetailsBinTestService
  include GRPC::GenericService
  rpc :an_rpc, EchoMsg, EchoMsg

  def an_rpc(_, _)
    fail GRPC::Unknown
  end
end

NoStatusDetailsBinTestServiceStub = NoStatusDetailsBinTestService.rpc_stub_class

describe 'when the endpoint doesnt send grpc-status-details-bin' do
  def start_server
    @srv = new_rpc_server_for_testing(pool_size: 1)
    @server_port = @srv.add_http2_port('localhost:0',
                                       :this_port_is_insecure)
    @srv.handle(NoStatusDetailsBinTestService)
    @server_thd = Thread.new { @srv.run }
    @srv.wait_till_running
  end

  def stop_server
    expect(@srv.stopped?).to be(false)
    @srv.stop
    @server_thd.join
    expect(@srv.stopped?).to be(true)
  end

  before(:each) do
    start_server
  end

  after(:each) do
    stop_server
  end

  it 'should receive nil when we extract try to extract a google '\
    'rpc status from a BadStatus exception that didnt have it' do
    stub = NoStatusDetailsBinTestServiceStub.new("localhost:#{@server_port}",
                                                 :this_channel_is_insecure)
    begin
      stub.an_rpc(EchoMsg.new)
    rescue GRPC::Unknown => e
      rpc_status = GRPC::GoogleRpcStatusUtils.extract_google_rpc_status(
        e.to_status)
    end
    expect(rpc_status).to be(nil)
  end

  it 'should receive nil when we extract try to extract a google '\
    'rpc status from an op views status object that didnt have it' do
    stub = NoStatusDetailsBinTestServiceStub.new("localhost:#{@server_port}",
                                                 :this_channel_is_insecure)
    op = stub.an_rpc(EchoMsg.new, return_op: true)
    begin
      op.execute
    rescue GRPC::Unknown => e
      status_from_exception = e.to_status
    end
    expect(GRPC::GoogleRpcStatusUtils.extract_google_rpc_status(
             status_from_exception)).to be(nil)
    expect(GRPC::GoogleRpcStatusUtils.extract_google_rpc_status(
             op.status)).to be nil
  end
end
