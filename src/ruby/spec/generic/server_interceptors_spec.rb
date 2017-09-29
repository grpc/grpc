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
require 'spec_helper'

describe 'Server Interceptors' do
  let(:interceptor) { TestServerInterceptor.new }
  let(:request) { EchoMsg.new }
  let(:trailing_metadata) { {} }
  let(:service) { EchoService.new(trailing_metadata) }
  let(:interceptors) { [] }

  before(:each) do
    build_rpc_server(server_opts: { interceptors: interceptors })
  end

  context 'when a server interceptor is added' do
    let(:interceptors) { [interceptor] }
    let(:client_metadata) { { client_md: 'test' } }
    let(:client_call_opts) { { metadata: client_metadata, return_op: true } }

    context 'with a request/response call' do
      let(:trailing_metadata) { { server_om: 'from_request_response' } }

      it 'should be called', server: true do
        expect(interceptor).to receive(:request_response)
          .once.and_call_original

        run_services_on_server(@server, services: [service]) do
          stub = build_insecure_stub(EchoStub)
          expect(stub.an_rpc(request)).to be_a(EchoMsg)
        end
      end

      it 'can modify trailing metadata', server: true do
        expect(interceptor).to receive(:request_response)
          .once.and_call_original

        run_services_on_server(@server, services: [service]) do
          stub = build_insecure_stub(EchoStub)
          expect_any_instance_of(GRPC::ActiveCall).to(
            receive(:request_response).with(request, metadata: client_metadata)
              .once.and_call_original
          )
          op = stub.an_rpc(request, client_call_opts)
          msg = op.execute
          expect(op.trailing_metadata).to eq(
            'interc' => 'from_request_response',
            'server_om' => 'from_request_response'
          )
          expect(msg).to be_a(EchoMsg)
        end
      end
    end

    context 'with a client streaming call' do
      let(:trailing_metadata) { { server_om: 'from_client_streamer' } }
      let(:requests) { [EchoMsg.new, EchoMsg.new] }

      it 'should be called', server: true do
        expect(interceptor).to receive(:client_streamer)
          .once.and_call_original

        run_services_on_server(@server, services: [service]) do
          stub = build_insecure_stub(EchoStub)
          expect(stub.a_client_streaming_rpc(requests)).to be_a(EchoMsg)
        end
      end

      it 'can modify trailing metadata', server: true do
        expect(interceptor).to receive(:client_streamer)
          .once.and_call_original

        run_services_on_server(@server, services: [service]) do
          stub = build_insecure_stub(EchoStub)
          expect_any_instance_of(GRPC::ActiveCall).to(
            receive(:client_streamer).with(requests)
              .once.and_call_original
          )
          op = stub.a_client_streaming_rpc(requests, client_call_opts)
          msg = op.execute
          expect(op.trailing_metadata).to eq(
            'interc' => 'from_client_streamer',
            'server_om' => 'from_client_streamer'
          )
          expect(msg).to be_a(EchoMsg)
        end
      end
    end

    context 'with a server streaming call' do
      let(:trailing_metadata) { { server_om: 'from_server_streamer' } }
      let(:request) { EchoMsg.new }

      it 'should be called', server: true do
        expect(interceptor).to receive(:server_streamer)
          .once.and_call_original

        run_services_on_server(@server, services: [service]) do
          stub = build_insecure_stub(EchoStub)
          responses = stub.a_server_streaming_rpc(request)
          responses.each do |r|
            expect(r).to be_a(EchoMsg)
          end
        end
      end

      it 'can modify trailing metadata', server: true do
        expect(interceptor).to receive(:server_streamer)
          .once.and_call_original

        run_services_on_server(@server, services: [service]) do
          stub = build_insecure_stub(EchoStub)
          expect_any_instance_of(GRPC::ActiveCall).to(
            receive(:server_streamer).with(request)
              .once.and_call_original
          )
          op = stub.a_server_streaming_rpc(request, client_call_opts)
          responses = op.execute
          responses.each do |r|
            expect(r).to be_a(EchoMsg)
          end
          expect(op.trailing_metadata).to eq(
            'interc' => 'from_server_streamer',
            'server_om' => 'from_server_streamer'
          )
        end
      end
    end

    context 'with a bidi call' do
      let(:trailing_metadata) { { server_om: 'from_bidi_streamer' } }
      let(:requests) { [EchoMsg.new, EchoMsg.new] }

      it 'should be called', server: true do
        expect(interceptor).to receive(:bidi_streamer)
          .once.and_call_original

        run_services_on_server(@server, services: [service]) do
          stub = build_insecure_stub(EchoStub)
          responses = stub.a_bidi_rpc(requests)
          responses.each do |r|
            expect(r).to be_a(EchoMsg)
          end
        end
      end

      it 'can modify trailing metadata', server: true do
        expect(interceptor).to receive(:bidi_streamer)
          .once.and_call_original

        run_services_on_server(@server, services: [service]) do
          stub = build_insecure_stub(EchoStub)
          expect_any_instance_of(GRPC::ActiveCall).to(
            receive(:bidi_streamer).with(requests)
              .once.and_call_original
          )
          op = stub.a_bidi_rpc(requests, client_call_opts)
          responses = op.execute
          responses.each do |r|
            expect(r).to be_a(EchoMsg)
          end
          expect(op.trailing_metadata).to eq(
            'interc' => 'from_bidi_streamer',
            'server_om' => 'from_bidi_streamer'
          )
        end
      end
    end
  end

  context 'when multiple interceptors are added' do
    let(:interceptor2) { TestServerInterceptor.new }
    let(:interceptor3) { TestServerInterceptor.new }
    let(:interceptors) do
      [
        interceptor,
        interceptor2,
        interceptor3
      ]
    end

    it 'each should be called', server: true do
      expect(interceptor).to receive(:request_response)
        .once.and_call_original
      expect(interceptor2).to receive(:request_response)
        .once.and_call_original
      expect(interceptor3).to receive(:request_response)
        .once.and_call_original

      run_services_on_server(@server, services: [service]) do
        stub = build_insecure_stub(EchoStub)
        expect(stub.an_rpc(request)).to be_a(EchoMsg)
      end
    end
  end

  context 'when an interceptor is not added' do
    it 'should not be called', server: true do
      expect(interceptor).to_not receive(:call)

      run_services_on_server(@server, services: [service]) do
        stub = build_insecure_stub(EchoStub)
        expect(stub.an_rpc(request)).to be_a(EchoMsg)
      end
    end
  end
end
