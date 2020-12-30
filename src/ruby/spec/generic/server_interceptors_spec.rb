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
  let(:request) { EchoMsg.new }
  let(:trailing_metadata) { {} }
  let(:service) { EchoService.new(**trailing_metadata) }

  context 'when a server interceptor is added' do
    let(:interceptors) { [@interceptor] }
    let(:client_metadata) { { 'client_md' => 'test' } }
    let(:client_call_opts) { { metadata: client_metadata, return_op: true } }

    context 'with a request/response call' do
      let(:trailing_metadata) { { server_om: 'from_request_response' } }

      it 'should be called', server: true do
        interceptor = TestServerInterceptor.new
        expect(interceptor.request_counts[:request_response]).to be 0
        build_rpc_server(server_opts: { interceptors: [interceptor] })
        run_services_on_server(@server, services: [service]) do
          stub = build_insecure_stub(EchoStub)
          expect(stub.an_rpc(request)).to be_a(EchoMsg)
        end
        expect(interceptor.request_counts[:request_response]).to be 1
      end

      it 'can modify trailing metadata', server: true do
        interceptor = TestServerInterceptor.new
        expect(interceptor.request_counts[:request_response]).to be 0
        build_rpc_server(server_opts: { interceptors: [interceptor] })
        run_services_on_server(@server, services: [service]) do
          stub = build_insecure_stub(EchoStub)
          op = stub.an_rpc(request, client_call_opts)
          msg = op.execute
          md_without_user_agent = op.metadata
          md_without_user_agent.delete('user-agent')
          expect(md_without_user_agent).to eq client_metadata # EchoService echos metadata
          expect(op.trailing_metadata).to eq(
            'interc' => 'from_request_response',
            'server_om' => 'from_request_response'
          )
          expect(msg).to be_a(EchoMsg)
        end
        expect(interceptor.request_counts[:request_response]).to be 1
      end
    end

    context 'with a client streaming call' do
      let(:trailing_metadata) { { server_om: 'from_client_streamer' } }
      let(:requests) { [EchoMsg.new, EchoMsg.new] }

      it 'should be called', server: true do
        interceptor = TestServerInterceptor.new
        expect(interceptor.request_counts[:client_streamer]).to be 0
        build_rpc_server(server_opts: { interceptors: [interceptor] })
        run_services_on_server(@server, services: [service]) do
          stub = build_insecure_stub(EchoStub)
          expect(stub.a_client_streaming_rpc(requests)).to be_a(EchoMsg)
        end
        expect(interceptor.request_counts[:client_streamer]).to be 1
      end

      it 'can modify trailing metadata', server: true do
        interceptor = TestServerInterceptor.new
        expect(interceptor.request_counts[:client_streamer]).to be 0
        build_rpc_server(server_opts: { interceptors: [interceptor] })
        run_services_on_server(@server, services: [service]) do
          stub = build_insecure_stub(EchoStub)
          op = stub.a_client_streaming_rpc(requests, client_call_opts)
          msg = op.execute
          expect(op.trailing_metadata).to eq(
            'interc' => 'from_client_streamer',
            'server_om' => 'from_client_streamer'
          )
          expect(msg).to be_a(EchoMsg)
        end
        expect(interceptor.request_counts[:client_streamer]).to be 1
      end
    end

    context 'with a server streaming call' do
      let(:trailing_metadata) { { server_om: 'from_server_streamer' } }
      let(:request) { EchoMsg.new }

      it 'should be called', server: true do
        interceptor = TestServerInterceptor.new
        expect(interceptor.request_counts[:server_streamer]).to be 0
        build_rpc_server(server_opts: { interceptors: [interceptor] })
        run_services_on_server(@server, services: [service]) do
          stub = build_insecure_stub(EchoStub)
          responses = stub.a_server_streaming_rpc(request)
          responses.each do |r|
            expect(r).to be_a(EchoMsg)
          end
        end
        expect(interceptor.request_counts[:server_streamer]).to be 1
      end

      it 'can modify trailing metadata', server: true do
        interceptor = TestServerInterceptor.new
        expect(interceptor.request_counts[:server_streamer]).to be 0
        build_rpc_server(server_opts: { interceptors: [interceptor] })
        run_services_on_server(@server, services: [service]) do
          stub = build_insecure_stub(EchoStub)
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
        expect(interceptor.request_counts[:server_streamer]).to be 1
      end
    end

    context 'with a bidi call' do
      let(:trailing_metadata) { { server_om: 'from_bidi_streamer' } }
      let(:requests) { [EchoMsg.new, EchoMsg.new] }

      it 'should be called', server: true do
        interceptor = TestServerInterceptor.new
        expect(interceptor.request_counts[:bidi_streamer]).to be 0
        build_rpc_server(server_opts: { interceptors: [interceptor] })
        run_services_on_server(@server, services: [service]) do
          stub = build_insecure_stub(EchoStub)
          responses = stub.a_bidi_rpc(requests)
          responses.each do |r|
            expect(r).to be_a(EchoMsg)
          end
        end
        expect(interceptor.request_counts[:bidi_streamer]).to be 1
      end

      it 'can modify trailing metadata', server: true do
        interceptor = TestServerInterceptor.new
        expect(interceptor.request_counts[:bidi_streamer]).to be 0
        build_rpc_server(server_opts: { interceptors: [interceptor] })
        run_services_on_server(@server, services: [service]) do
          stub = build_insecure_stub(EchoStub)
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
        expect(interceptor.request_counts[:bidi_streamer]).to be 1
      end
    end
  end

  context 'when multiple interceptors are added' do
    it 'each should be called', server: true do
      interceptors = [
        TestServerInterceptor.new,
        TestServerInterceptor.new,
        TestServerInterceptor.new
      ]
      interceptors.each do |i|
        expect(i.request_counts[:request_response]).to be 0
      end
      build_rpc_server(server_opts: { interceptors: interceptors })
      run_services_on_server(@server, services: [service]) do
        stub = build_insecure_stub(EchoStub)
        expect(stub.an_rpc(request)).to be_a(EchoMsg)
      end
      interceptors.each do |i|
        expect(i.request_counts[:request_response]).to be 1
      end
    end
  end
end
