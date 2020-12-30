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
  let(:request) { EchoMsg.new }
  let(:service) { EchoService }

  before(:each) do
    build_rpc_server
  end

  context 'when a client interceptor is added' do
    context 'with a request/response call' do
      it 'should be called', server: true, test: true do
        interceptor = TestClientInterceptor.new
        expect(interceptor.request_counts[:request_response]).to be 0
        run_services_on_server(@server, services: [service]) do
          stub = build_insecure_stub(EchoStub, opts: { interceptors: [interceptor] })
          expect(stub.an_rpc(request)).to be_a(EchoMsg)
        end
        expect(interceptor.request_counts[:request_response]).to be 1
      end

      it 'can modify outgoing metadata', server: true, test: true do
        interceptor = TestClientInterceptor.new
        expect(interceptor.request_counts[:request_response]).to be 0
        run_services_on_server(@server, services: [service]) do
          stub = build_insecure_stub(EchoStub, opts: { interceptors: [interceptor] })
          op = stub.an_rpc(request, return_op: true)
          expect(op.execute).to be_a(EchoMsg)
          md_without_user_agent = op.metadata
          md_without_user_agent.delete('user-agent')
          expect(md_without_user_agent).to eq('foo' => 'bar_from_request_response')
        end
        expect(interceptor.request_counts[:request_response]).to be 1
      end
    end

    context 'with a client streaming call' do
      it 'should be called', server: true do
        interceptor = TestClientInterceptor.new
        expect(interceptor.request_counts[:client_streamer]).to be 0
        run_services_on_server(@server, services: [service]) do
          stub = build_insecure_stub(EchoStub, opts: { interceptors: [interceptor] })
          requests = [EchoMsg.new, EchoMsg.new]
          expect(stub.a_client_streaming_rpc(requests)).to be_a(EchoMsg)
        end
        expect(interceptor.request_counts[:client_streamer]).to be 1
      end

      it 'can modify outgoing metadata', server: true do
        interceptor = TestClientInterceptor.new
        expect(interceptor.request_counts[:client_streamer]).to be 0
        run_services_on_server(@server, services: [service]) do
          stub = build_insecure_stub(EchoStub, opts: { interceptors: [interceptor] })
          requests = [EchoMsg.new, EchoMsg.new]
          op = stub.a_client_streaming_rpc(requests, return_op: true)
          expect(op.execute).to be_a(EchoMsg)
          md_without_user_agent = op.metadata
          md_without_user_agent.delete('user-agent')
          expect(md_without_user_agent).to eq('foo' => 'bar_from_client_streamer')
        end
        expect(interceptor.request_counts[:client_streamer]).to be 1
      end
    end

    context 'with a server streaming call' do
      it 'should be called', server: true do
        interceptor = TestClientInterceptor.new
        expect(interceptor.request_counts[:server_streamer]).to be 0
        run_services_on_server(@server, services: [service]) do
          stub = build_insecure_stub(EchoStub, opts: { interceptors: [interceptor] })
          request = EchoMsg.new
          responses = stub.a_server_streaming_rpc(request)
          responses.each do |r|
            expect(r).to be_a(EchoMsg)
          end
        end
        expect(interceptor.request_counts[:server_streamer]).to be 1
      end

      it 'can modify outgoing metadata', server: true do
        interceptor = TestClientInterceptor.new
        expect(interceptor.request_counts[:server_streamer]).to be 0
        run_services_on_server(@server, services: [service]) do
          stub = build_insecure_stub(EchoStub, opts: { interceptors: [interceptor] })
          request = EchoMsg.new
          op = stub.a_server_streaming_rpc(request, return_op: true)
          responses = op.execute
          responses.each do |r|
            expect(r).to be_a(EchoMsg)
          end
          md_without_user_agent = op.metadata
          md_without_user_agent.delete('user-agent')
          expect(md_without_user_agent).to eq('foo' => 'bar_from_server_streamer')
        end
        expect(interceptor.request_counts[:server_streamer]).to be 1
      end
    end

    context 'with a bidi call' do
      it 'should be called', server: true do
        interceptor = TestClientInterceptor.new
        expect(interceptor.request_counts[:bidi_streamer]).to be 0
        run_services_on_server(@server, services: [service]) do
          stub = build_insecure_stub(EchoStub, opts: { interceptors: [interceptor] })
          requests = [EchoMsg.new, EchoMsg.new]
          responses = stub.a_bidi_rpc(requests)
          responses.each do |r|
            expect(r).to be_a(EchoMsg)
          end
        end
        expect(interceptor.request_counts[:bidi_streamer]).to be 1
      end

      it 'can modify outgoing metadata', server: true do
        interceptor = TestClientInterceptor.new
        expect(interceptor.request_counts[:bidi_streamer]).to be 0
        run_services_on_server(@server, services: [service]) do
          stub = build_insecure_stub(EchoStub, opts: { interceptors: [interceptor] })
          requests = [EchoMsg.new, EchoMsg.new]
          op = stub.a_bidi_rpc(requests, return_op: true)
          responses = op.execute
          responses.each do |r|
            expect(r).to be_a(EchoMsg)
          end
          md_without_user_agent = op.metadata
          md_without_user_agent.delete('user-agent')
          # EchoService echos metadata
          expect(md_without_user_agent).to eq('foo' => 'bar_from_bidi_streamer')
        end
        expect(interceptor.request_counts[:bidi_streamer]).to be 1
      end
    end
  end
end
