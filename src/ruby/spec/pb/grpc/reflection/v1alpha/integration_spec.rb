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
require 'grpc/reflection/v1alpha/server'
require 'grpc/health/checker'

RSpec.describe 'Server Reflection integration' do
  before(:all) do
    @rpc_server = GRPC::RpcServer.new
    @rpc_server.handle(Grpc::Health::Checker.new)
    @rpc_server.handle(
      Grpc::Reflection::V1alpha::Server.new(
        %w[grpc.health.v1.Health grpc.reflection.v1alpha.ServerReflection]
      )
    )
    @port = @rpc_server.add_http2_port('0.0.0.0:0', :this_port_is_insecure)
    @server_thread = Thread.new { @rpc_server.run }
    @rpc_server.wait_till_running
  end

  after(:all) do
    @rpc_server.stop
    @server_thread.join
  end

  let(:stub) do
    Grpc::Reflection::V1alpha::ServerReflection::Stub.new(
      "0.0.0.0:#{@port}", :this_channel_is_insecure
    )
  end

  def send_request(**fields)
    req = Grpc::Reflection::V1alpha::ServerReflectionRequest.new(**fields)
    stub.server_reflection_info([req]).first
  end

  it 'lists registered services' do
    resp = send_request(list_services: '')
    names = resp.list_services_response.service.map(&:name)
    expect(names).to include('grpc.health.v1.Health', 'grpc.reflection.v1alpha.ServerReflection')
  end

  it 'fetches file descriptor by symbol' do
    resp = send_request(file_containing_symbol: 'grpc.health.v1.Health')
    fd = Google::Protobuf::FileDescriptorProto.decode(
      resp.file_descriptor_response.file_descriptor_proto.first
    )
    expect(fd.name).to eq('grpc/health/v1/health.proto')
  end

  it 'fetches file descriptor by filename' do
    resp = send_request(file_by_filename: 'grpc/health/v1/health.proto')
    expect(resp.file_descriptor_response.file_descriptor_proto).not_to be_empty
  end
end
