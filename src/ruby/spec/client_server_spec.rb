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

require 'grpc'

include GRPC::Core

shared_context 'setup: tags' do
  let(:sent_message) { 'sent message' }
  let(:reply_text) { 'the reply' }

  def deadline
    Time.now + 5
  end

  def server_allows_client_to_proceed(metadata = {})
    recvd_rpc = @server.request_call
    expect(recvd_rpc).to_not eq nil
    server_call = recvd_rpc.call
    ops = { CallOps::SEND_INITIAL_METADATA => metadata }
    svr_batch = server_call.run_batch(ops)
    expect(svr_batch.send_metadata).to be true
    server_call
  end

  def new_client_call
    @ch.create_call(nil, nil, '/method', nil, deadline)
  end
end

shared_examples 'basic GRPC message delivery is OK' do
  include GRPC::Core
  include_context 'setup: tags'

  context 'the test channel' do
    it 'should have a target' do
      expect(@ch.target).to be_a(String)
    end
  end

  context 'a client call' do
    it 'should have a peer' do
      expect(new_client_call.peer).to be_a(String)
    end
  end

  it 'calls have peer info' do
    call = new_client_call
    expect(call.peer).to be_a(String)
  end

  it 'servers receive requests from clients and can respond' do
    call = new_client_call
    server_call = nil

    server_thread = Thread.new do
      server_call = server_allows_client_to_proceed
    end

    client_ops = {
      CallOps::SEND_INITIAL_METADATA => {},
      CallOps::SEND_MESSAGE => sent_message
    }
    batch_result = call.run_batch(client_ops)
    expect(batch_result.send_metadata).to be true
    expect(batch_result.send_message).to be true

    # confirm the server can read the inbound message
    server_thread.join
    server_ops = {
      CallOps::RECV_MESSAGE => nil
    }
    svr_batch = server_call.run_batch(server_ops)
    expect(svr_batch.message).to eq(sent_message)
  end

  it 'responses written by servers are received by the client' do
    call = new_client_call
    server_call = nil

    server_thread = Thread.new do
      server_call = server_allows_client_to_proceed
    end

    client_ops = {
      CallOps::SEND_INITIAL_METADATA => {},
      CallOps::SEND_MESSAGE => sent_message
    }
    batch_result = call.run_batch(client_ops)
    expect(batch_result.send_metadata).to be true
    expect(batch_result.send_message).to be true

    # confirm the server can read the inbound message
    server_thread.join
    server_ops = {
      CallOps::RECV_MESSAGE => nil,
      CallOps::SEND_MESSAGE => reply_text
    }
    svr_batch = server_call.run_batch(server_ops)
    expect(svr_batch.message).to eq(sent_message)
    expect(svr_batch.send_message).to be true
  end

  it 'compressed messages can be sent and received' do
    call = new_client_call
    server_call = nil
    long_request_str = '0' * 2000
    long_response_str = '1' * 2000
    md = { 'grpc-internal-encoding-request' => 'gzip' }

    server_thread = Thread.new do
      server_call = server_allows_client_to_proceed(md)
    end

    client_ops = {
      CallOps::SEND_INITIAL_METADATA => md,
      CallOps::SEND_MESSAGE => long_request_str
    }
    batch_result = call.run_batch(client_ops)
    expect(batch_result.send_metadata).to be true
    expect(batch_result.send_message).to be true

    # confirm the server can read the inbound message
    server_thread.join
    server_ops = {
      CallOps::RECV_MESSAGE => nil,
      CallOps::SEND_MESSAGE => long_response_str
    }
    svr_batch = server_call.run_batch(server_ops)
    expect(svr_batch.message).to eq(long_request_str)
    expect(svr_batch.send_message).to be true

    client_ops = {
      CallOps::SEND_CLOSE_FROM_CLIENT => nil,
      CallOps::RECV_INITIAL_METADATA => nil,
      CallOps::RECV_MESSAGE => nil
    }
    batch_result = call.run_batch(client_ops)
    expect(batch_result.send_close).to be true
    expect(batch_result.message).to eq long_response_str
  end

  it 'servers can ignore a client write and send a status' do
    call = new_client_call
    server_call = nil

    server_thread = Thread.new do
      server_call = server_allows_client_to_proceed
    end

    client_ops = {
      CallOps::SEND_INITIAL_METADATA => {},
      CallOps::SEND_MESSAGE => sent_message
    }
    batch_result = call.run_batch(client_ops)
    expect(batch_result.send_metadata).to be true
    expect(batch_result.send_message).to be true

    # confirm the server can read the inbound message
    the_status = Struct::Status.new(StatusCodes::OK, 'OK')
    server_thread.join
    server_ops = {
      CallOps::SEND_STATUS_FROM_SERVER => the_status
    }
    svr_batch = server_call.run_batch(server_ops)
    expect(svr_batch.message).to eq nil
    expect(svr_batch.send_status).to be true
  end

  it 'completes calls by sending status to client and server' do
    call = new_client_call
    server_call = nil

    server_thread = Thread.new do
      server_call = server_allows_client_to_proceed
    end

    client_ops = {
      CallOps::SEND_INITIAL_METADATA => {},
      CallOps::SEND_MESSAGE => sent_message
    }
    batch_result = call.run_batch(client_ops)
    expect(batch_result.send_metadata).to be true
    expect(batch_result.send_message).to be true

    # confirm the server can read the inbound message and respond
    the_status = Struct::Status.new(StatusCodes::OK, 'OK', {})
    server_thread.join
    server_ops = {
      CallOps::RECV_MESSAGE => nil,
      CallOps::SEND_MESSAGE => reply_text,
      CallOps::SEND_STATUS_FROM_SERVER => the_status
    }
    svr_batch = server_call.run_batch(server_ops)
    expect(svr_batch.message).to eq sent_message
    expect(svr_batch.send_status).to be true
    expect(svr_batch.send_message).to be true

    # confirm the client can receive the server response and status.
    client_ops = {
      CallOps::SEND_CLOSE_FROM_CLIENT => nil,
      CallOps::RECV_INITIAL_METADATA => nil,
      CallOps::RECV_MESSAGE => nil,
      CallOps::RECV_STATUS_ON_CLIENT => nil
    }
    batch_result = call.run_batch(client_ops)
    expect(batch_result.send_close).to be true
    expect(batch_result.message).to eq reply_text
    expect(batch_result.status).to eq the_status

    # confirm the server can receive the client close.
    server_ops = {
      CallOps::RECV_CLOSE_ON_SERVER => nil
    }
    svr_batch = server_call.run_batch(server_ops)
    expect(svr_batch.send_close).to be true
  end
end

shared_examples 'GRPC metadata delivery works OK' do
  include_context 'setup: tags'

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
        call = new_client_call
        client_ops = {
          CallOps::SEND_INITIAL_METADATA => md
        }
        blk = proc do
          call.run_batch(client_ops)
        end
        expect(&blk).to raise_error
      end
    end

    it 'sends all the metadata pairs when keys and values are valid' do
      @valid_metadata.each do |md|
        recvd_rpc = nil
        rcv_thread = Thread.new do
          recvd_rpc = @server.request_call
        end

        call = new_client_call
        client_ops = {
          CallOps::SEND_INITIAL_METADATA => md
        }
        batch_result = call.run_batch(client_ops)
        expect(batch_result.send_metadata).to be true

        # confirm the server can receive the client metadata
        rcv_thread.join
        expect(recvd_rpc).to_not eq nil
        recvd_md = recvd_rpc.metadata
        replace_symbols = Hash[md.each_pair.collect { |x, y| [x.to_s, y] }]
        expect(recvd_md).to eq(recvd_md.merge(replace_symbols))
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
      @bad_keys.each do |md|
        recvd_rpc = nil
        rcv_thread = Thread.new do
          recvd_rpc = @server.request_call
        end

        call = new_client_call
        # client signals that it's done sending metadata to allow server to
        # respond
        client_ops = {
          CallOps::SEND_INITIAL_METADATA => nil
        }
        call.run_batch(client_ops)

        # server gets the invocation
        rcv_thread.join
        expect(recvd_rpc).to_not eq nil
        server_ops = {
          CallOps::SEND_INITIAL_METADATA => md
        }
        blk = proc do
          recvd_rpc.call.run_batch(server_ops)
        end
        expect(&blk).to raise_error
      end
    end

    it 'sends an empty hash if no metadata is added' do
      recvd_rpc = nil
      rcv_thread = Thread.new do
        recvd_rpc = @server.request_call
      end

      call = new_client_call
      # client signals that it's done sending metadata to allow server to
      # respond
      client_ops = {
        CallOps::SEND_INITIAL_METADATA => nil
      }
      call.run_batch(client_ops)

      # server gets the invocation but sends no metadata back
      rcv_thread.join
      expect(recvd_rpc).to_not eq nil
      server_call = recvd_rpc.call
      server_ops = {
        CallOps::SEND_INITIAL_METADATA => nil
      }
      server_call.run_batch(server_ops)

      # client receives nothing as expected
      client_ops = {
        CallOps::RECV_INITIAL_METADATA => nil
      }
      batch_result = call.run_batch(client_ops)
      expect(batch_result.metadata).to eq({})
    end

    it 'sends all the pairs when keys and values are valid' do
      @valid_metadata.each do |md|
        recvd_rpc = nil
        rcv_thread = Thread.new do
          recvd_rpc = @server.request_call
        end

        call = new_client_call
        # client signals that it's done sending metadata to allow server to
        # respond
        client_ops = {
          CallOps::SEND_INITIAL_METADATA => nil
        }
        call.run_batch(client_ops)

        # server gets the invocation but sends no metadata back
        rcv_thread.join
        expect(recvd_rpc).to_not eq nil
        server_call = recvd_rpc.call
        server_ops = {
          CallOps::SEND_INITIAL_METADATA => md
        }
        server_call.run_batch(server_ops)

        # client receives nothing as expected
        client_ops = {
          CallOps::RECV_INITIAL_METADATA => nil
        }
        batch_result = call.run_batch(client_ops)
        replace_symbols = Hash[md.each_pair.collect { |x, y| [x.to_s, y] }]
        expect(batch_result.metadata).to eq(replace_symbols)
      end
    end
  end
end

describe 'the http client/server' do
  before(:example) do
    server_host = '0.0.0.0:0'
    @server = GRPC::Core::Server.new(nil)
    server_port = @server.add_http2_port(server_host, :this_port_is_insecure)
    @server.start
    @ch = Channel.new("0.0.0.0:#{server_port}", nil, :this_channel_is_insecure)
  end

  after(:example) do
    @ch.close
    @server.close(deadline)
  end

  it_behaves_like 'basic GRPC message delivery is OK' do
  end

  it_behaves_like 'GRPC metadata delivery works OK' do
  end
end

describe 'the secure http client/server' do
  include_context 'setup: tags'

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
    @server = GRPC::Core::Server.new(nil)
    server_port = @server.add_http2_port(server_host, server_creds)
    @server.start
    args = { Channel::SSL_TARGET => 'foo.test.google.fr' }
    @ch = Channel.new("0.0.0.0:#{server_port}", args,
                      GRPC::Core::ChannelCredentials.new(certs[0], nil, nil))
  end

  after(:example) do
    @server.close(deadline)
  end

  it_behaves_like 'basic GRPC message delivery is OK' do
  end

  it_behaves_like 'GRPC metadata delivery works OK' do
  end

  it 'modifies metadata with CallCredentials' do
    auth_proc = proc { { 'k1' => 'updated-v1' } }
    call_creds = GRPC::Core::CallCredentials.new(auth_proc)
    md = { 'k2' => 'v2' }
    expected_md = { 'k1' => 'updated-v1', 'k2' => 'v2' }
    recvd_rpc = nil
    rcv_thread = Thread.new do
      recvd_rpc = @server.request_call
    end

    call = new_client_call
    call.set_credentials! call_creds
    client_ops = {
      CallOps::SEND_INITIAL_METADATA => md
    }
    batch_result = call.run_batch(client_ops)
    expect(batch_result.send_metadata).to be true

    # confirm the server can receive the client metadata
    rcv_thread.join
    expect(recvd_rpc).to_not eq nil
    recvd_md = recvd_rpc.metadata
    replace_symbols = Hash[expected_md.each_pair.collect { |x, y| [x.to_s, y] }]
    expect(recvd_md).to eq(recvd_md.merge(replace_symbols))
  end
end
