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

describe 'Client Interceptors' do
  let(:interceptor) { TestClientInterceptor.new }
  let(:interceptors_opts) { { interceptors: [interceptor] } }
  let(:request) { EchoMsg.new }
  let(:service) { EchoService }

  before(:each) do
    build_rpc_server
  end

  context 'when a client interceptor is added' do
    context 'with a request/response call' do
      it 'should be called', server: true do
        expect(interceptor).to receive(:request_response)
          .once.and_call_original

        run_services_on_server(@server, services: [service]) do
          stub = build_insecure_stub(EchoStub, opts: interceptors_opts)
          expect_any_instance_of(GRPC::ActiveCall).to receive(:request_response)
            .once.and_call_original
          expect(stub.an_rpc(request)).to be_a(EchoMsg)
        end
      end

      it 'can modify outgoing metadata', server: true do
        expect(interceptor).to receive(:request_response)
          .once.and_call_original

        run_services_on_server(@server, services: [service]) do
          stub = build_insecure_stub(EchoStub, opts: interceptors_opts)
          expect_any_instance_of(GRPC::ActiveCall).to receive(:request_response)
            .with(request, metadata: { 'foo' => 'bar_from_request_response' })
            .once.and_call_original
          expect(stub.an_rpc(request)).to be_a(EchoMsg)
        end
      end
    end

    context 'with a client streaming call' do
      it 'should be called', server: true do
        expect(interceptor).to receive(:client_streamer)
          .once.and_call_original

        run_services_on_server(@server, services: [service]) do
          stub = build_insecure_stub(EchoStub, opts: interceptors_opts)
          expect_any_instance_of(GRPC::ActiveCall).to receive(:client_streamer)
            .once.and_call_original
          requests = [EchoMsg.new, EchoMsg.new]
          expect(stub.a_client_streaming_rpc(requests)).to be_a(EchoMsg)
        end
      end

      it 'can modify outgoing metadata', server: true do
        expect(interceptor).to receive(:client_streamer)
          .once.and_call_original

        run_services_on_server(@server, services: [service]) do
          stub = build_insecure_stub(EchoStub, opts: interceptors_opts)
          requests = [EchoMsg.new, EchoMsg.new]
          expect_any_instance_of(GRPC::ActiveCall).to receive(:client_streamer)
            .with(requests, metadata: { 'foo' => 'bar_from_client_streamer' })
            .once.and_call_original
          expect(stub.a_client_streaming_rpc(requests)).to be_a(EchoMsg)
        end
      end
    end

    context 'with a server streaming call' do
      it 'should be called', server: true do
        expect(interceptor).to receive(:server_streamer)
          .once.and_call_original

        run_services_on_server(@server, services: [service]) do
          stub = build_insecure_stub(EchoStub, opts: interceptors_opts)
          request = EchoMsg.new
          expect_any_instance_of(GRPC::ActiveCall).to receive(:server_streamer)
            .once.and_call_original
          responses = stub.a_server_streaming_rpc(request)
          responses.each do |r|
            expect(r).to be_a(EchoMsg)
          end
        end
      end

      it 'can modify outgoing metadata', server: true do
        expect(interceptor).to receive(:server_streamer)
          .once.and_call_original

        run_services_on_server(@server, services: [service]) do
          stub = build_insecure_stub(EchoStub, opts: interceptors_opts)
          request = EchoMsg.new
          expect_any_instance_of(GRPC::ActiveCall).to receive(:server_streamer)
            .with(request, metadata: { 'foo' => 'bar_from_server_streamer' })
            .once.and_call_original
          responses = stub.a_server_streaming_rpc(request)
          responses.each do |r|
            expect(r).to be_a(EchoMsg)
          end
        end
      end
    end

    context 'with a bidi call' do
      it 'should be called', server: true do
        expect(interceptor).to receive(:bidi_streamer)
          .once.and_call_original

        run_services_on_server(@server, services: [service]) do
          stub = build_insecure_stub(EchoStub, opts: interceptors_opts)
          expect_any_instance_of(GRPC::ActiveCall).to receive(:bidi_streamer)
            .once.and_call_original
          requests = [EchoMsg.new, EchoMsg.new]
          responses = stub.a_bidi_rpc(requests)
          responses.each do |r|
            expect(r).to be_a(EchoMsg)
          end
        end
      end

      it 'can modify outgoing metadata', server: true do
        expect(interceptor).to receive(:bidi_streamer)
          .once.and_call_original

        run_services_on_server(@server, services: [service]) do
          stub = build_insecure_stub(EchoStub, opts: interceptors_opts)
          requests = [EchoMsg.new, EchoMsg.new]
          expect_any_instance_of(GRPC::ActiveCall).to receive(:bidi_streamer)
            .with(requests, metadata: { 'foo' => 'bar_from_bidi_streamer' })
            .once.and_call_original
          responses = stub.a_bidi_rpc(requests)
          responses.each do |r|
            expect(r).to be_a(EchoMsg)
          end
        end
      end
    end
  end
end
