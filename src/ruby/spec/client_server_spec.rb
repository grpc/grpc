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

include GRPC::Core

shared_examples 'basic GRPC message delivery is OK' do
  include GRPC::Core

  context 'the test channel' do
    it 'should have a target' do
      expect(@ch.target).to be_a(String)
    end
  end

  it 'unary calls work' do
    run_services_on_server(@server, services: [EchoService]) do
      call = @stub.an_rpc(EchoMsg.new, return_op: true)
      expect(call.execute).to be_a(EchoMsg)
    end
  end

  it 'unary calls work when enabling compression' do
    run_services_on_server(@server, services: [EchoService]) do
      long_request_str = '0' * 2000
      md = { 'grpc-internal-encoding-request' => 'gzip' }
      call = @stub.an_rpc(EchoMsg.new(msg: long_request_str),
                          return_op: true,
                          metadata: md)
      response = call.execute
      expect(response).to be_a(EchoMsg)
      expect(response.msg).to eq(long_request_str)
    end
  end

  def client_cancel_test(cancel_proc, expected_code,
                         expected_details)
    call = @stub.an_rpc(EchoMsg.new, return_op: true)
    run_services_on_server(@server, services: [EchoService]) do
      # start the call, but don't send a message yet
      call.start_call
      # cancel the call
      cancel_proc.call(call)
      # check the client's status
      failed = false
      begin
        call.execute
      rescue GRPC::BadStatus => e
        failed = true
        expect(e.code).to be expected_code
        expect(e.details).to eq expected_details
      end
      expect(failed).to be(true)
    end
  end

  it 'clients can cancel a call on the server' do
    expected_code = StatusCodes::CANCELLED
    expected_details = 'CANCELLED'
    cancel_proc = proc { |call| call.cancel }
    client_cancel_test(cancel_proc, expected_code, expected_details)
  end

  it 'cancel_with_status unknown status' do
    code = StatusCodes::UNKNOWN
    details = 'test unknown reason'
    cancel_proc = proc { |call| call.cancel_with_status(code, details) }
    client_cancel_test(cancel_proc, code, details)
  end

  it 'cancel_with_status unknown status' do
    code = StatusCodes::FAILED_PRECONDITION
    details = 'test failed precondition reason'
    cancel_proc = proc { |call| call.cancel_with_status(code, details) }
    client_cancel_test(cancel_proc, code, details)
  end
end

shared_examples 'GRPC metadata delivery works OK' do
  describe 'from client => server' do
    before(:example) do
      n = 7  # arbitrary number of metadata
      diff_keys_fn = proc { |i| [format('k%d', i), format('v%d', i)] }
      diff_keys = Hash[n.times.collect { |x| diff_keys_fn.call x }]
      null_vals_fn = proc { |i| [format('k%d', i), format('v\0%d', i)] }
      null_vals = Hash[n.times.collect { |x| null_vals_fn.call x }]
      same_keys_fn = proc { |i| [format('k%d', i), [format('v%d', i)] * n] }
      same_keys = Hash[n.times.collect { |x| same_keys_fn.call x }]
      symbol_key = { a_key: 'a val' }
      @valid_metadata = [diff_keys, same_keys, null_vals, symbol_key]
      @bad_keys = []
      @bad_keys << { Object.new => 'a value' }
      @bad_keys << { 1 => 'a value' }
    end

    it 'raises an exception if a metadata key is invalid' do
      @bad_keys.each do |md|
        # NOTE: no need to run a server in this test b/c the failure
        # happens while validating metadata to send.
        failed = false
        begin
          @stub.an_rpc(EchoMsg.new, metadata: md)
        rescue TypeError => e
          failed = true
          expect(e.message).to eq('grpc_rb_md_ary_fill_hash_cb: bad type for key parameter')
        end
        expect(failed).to be(true)
      end
    end

    it 'sends all the metadata pairs when keys and values are valid' do
      service = EchoService.new
      run_services_on_server(@server, services: [service]) do
        @valid_metadata.each_with_index do |md, i|
          expect(@stub.an_rpc(EchoMsg.new, metadata: md)).to be_a(EchoMsg)
          # confirm the server can receive the client metadata
          # finish the call
          expect(service.received_md.length).to eq(i + 1)
          md.each do |k, v|
            expect(service.received_md[i][k.to_s]).to eq(v)
          end
        end
      end
    end
  end

  describe 'from server => client' do
    before(:example) do
      n = 7  # arbitrary number of metadata
      diff_keys_fn = proc { |i| [format('k%d', i), format('v%d', i)] }
      diff_keys = Hash[n.times.collect { |x| diff_keys_fn.call x }]
      null_vals_fn = proc { |i| [format('k%d', i), format('v\0%d', i)] }
      null_vals = Hash[n.times.collect { |x| null_vals_fn.call x }]
      same_keys_fn = proc { |i| [format('k%d', i), [format('v%d', i)] * n] }
      same_keys = Hash[n.times.collect { |x| same_keys_fn.call x }]
      symbol_key = { a_key: 'a val' }
      @valid_metadata = [diff_keys, same_keys, null_vals, symbol_key]
      @bad_keys = []
      @bad_keys << { Object.new => 'a value' }
      @bad_keys << { 1 => 'a value' }
    end

    it 'raises an exception if a metadata key is invalid' do
      service = EchoService.new
      run_services_on_server(@server, services: [service]) do
        @bad_keys.each do |md|
          proceed = Queue.new
          server_exception = nil
          service.on_call_started = proc do |call|
            call.send_initial_metadata(md)
          rescue TypeError => e
            server_exception = e
          ensure
            proceed.push(1)
          end
          client_exception = nil
          client_call = @stub.an_rpc(EchoMsg.new, return_op: true)
          thr = Thread.new do
            client_call.execute
          rescue GRPC::BadStatus => e
            client_exception = e
          end
          proceed.pop
          # TODO(apolcyn): we shouldn't need this cancel here. It's
          # only currently needed b/c the server does not seem to properly
          # terminate the RPC if it fails to send initial metadata. That
          # should be fixed, in which case this cancellation can be removed.
          client_call.cancel
          thr.join
          p client_exception
          expect(client_exception.nil?).to be(false)
          expect(server_exception.nil?).to be(false)
          expect(server_exception.message).to eq(
            'grpc_rb_md_ary_fill_hash_cb: bad type for key parameter')
        end
      end
    end

    it 'sends an empty hash if no metadata is added' do
      run_services_on_server(@server, services: [EchoService]) do
        call = @stub.an_rpc(EchoMsg.new, return_op: true)
        expect(call.execute).to be_a(EchoMsg)
        expect(call.metadata).to eq({})
      end
    end

    it 'sends all the pairs when keys and values are valid' do
      service = EchoService.new
      run_services_on_server(@server, services: [service]) do
        @valid_metadata.each do |md|
          service.on_call_started = proc do |call|
            call.send_initial_metadata(md)
          end
          call = @stub.an_rpc(EchoMsg.new, return_op: true)
          call.execute
          replace_symbols = Hash[md.each_pair.collect { |x, y| [x.to_s, y] }]
          expect(call.metadata).to eq(replace_symbols)
        end
      end
    end
  end
end

describe 'the http client/server' do
  before(:example) do
    server_host = '0.0.0.0:0'
    @server = new_rpc_server_for_testing
    server_port = @server.add_http2_port(server_host, :this_port_is_insecure)
    @ch = Channel.new("0.0.0.0:#{server_port}", nil, :this_channel_is_insecure)
    @stub = EchoStub.new(
      "0.0.0.0:#{server_port}", nil, channel_override: @ch)
  end

  it_behaves_like 'basic GRPC message delivery is OK' do
  end

  it_behaves_like 'GRPC metadata delivery works OK' do
  end
end

describe 'the secure http client/server' do
  def load_test_certs
    test_root = File.join(File.dirname(__FILE__), 'testdata')
    files = ['ca.pem', 'server1.key', 'server1.pem']
    files.map { |f| File.open(File.join(test_root, f)).read }
  end

  before(:example) do
    certs = load_test_certs
    server_host = '0.0.0.0:0'
    server_creds = GRPC::Core::ServerCredentials.new(
      nil, [{ private_key: certs[1], cert_chain: certs[2] }], false)
    @server = new_rpc_server_for_testing
    server_port = @server.add_http2_port(server_host, server_creds)
    args = { Channel::SSL_TARGET => 'foo.test.google.fr' }
    @ch = Channel.new(
      "0.0.0.0:#{server_port}", args,
      GRPC::Core::ChannelCredentials.new(certs[0], nil, nil))
    @stub = EchoStub.new(
      "0.0.0.0:#{server_port}", nil, channel_override: @ch)
  end

  it_behaves_like 'basic GRPC message delivery is OK' do
  end

  it_behaves_like 'GRPC metadata delivery works OK' do
  end

  it 'modifies metadata with CallCredentials' do
    # create call creds
    auth_proc = proc { { 'k1' => 'v1' } }
    call_creds = GRPC::Core::CallCredentials.new(auth_proc)
    # create arbitrary custom metadata
    custom_md = { 'k2' => 'v2' }
    # perform an RPC
    echo_service = EchoService.new
    run_services_on_server(@server, services: [echo_service]) do
      expect(@stub.an_rpc(EchoMsg.new,
                          credentials: call_creds,
                          metadata: custom_md)).to be_a(EchoMsg)
    end
    # call creds metadata should be merged with custom MD
    expect(echo_service.received_md.length).to eq(1)
    expected_md = { 'k1' => 'v1', 'k2' => 'v2' }
    expected_md.each do |k, v|
      expect(echo_service.received_md[0][k]).to eq(v)
    end
  end

  it 'modifies large metadata with CallCredentials' do
    val_array = %w(
      '00000000000000000000000000000000000000000000000000000000000000',
      '11111111111111111111111111111111111111111111111111111111111111',
    )
    # create call creds
    auth_proc = proc do
      {
        k2: val_array,
        k3: '0000000000000000000000000000000000000000000000000000000000',
        keeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeey4: 'v4'
      }
    end
    call_creds = GRPC::Core::CallCredentials.new(auth_proc)
    # create arbitrary custom metadata
    custom_md = { k1: 'v1' }
    # perform an RPC
    echo_service = EchoService.new
    run_services_on_server(@server, services: [echo_service]) do
      expect(@stub.an_rpc(EchoMsg.new,
                          credentials: call_creds,
                          metadata: custom_md)).to be_a(EchoMsg)
    end
    # call creds metadata should be merged with custom MD
    expect(echo_service.received_md.length).to eq(1)
    expected_md = {
      k1: 'v1',
      k2: val_array,
      k3: '0000000000000000000000000000000000000000000000000000000000',
      keeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeey4: 'v4'
    }
    expected_md.each do |k, v|
      expect(echo_service.received_md[0][k.to_s]).to eq(v)
    end
  end
end
