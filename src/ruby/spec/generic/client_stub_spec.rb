# Copyright 2015, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

require 'grpc'

Thread.abort_on_exception = true

def wakey_thread(&blk)
  n = GRPC::Notifier.new
  t = Thread.new do
    blk.call(n)
  end
  t.abort_on_exception = true
  n.wait
  t
end

def load_test_certs
  test_root = File.join(File.dirname(File.dirname(__FILE__)), 'testdata')
  files = ['ca.pem', 'server1.key', 'server1.pem']
  files.map { |f| File.open(File.join(test_root, f)).read }
end

include GRPC::Core::StatusCodes
include GRPC::Core::TimeConsts
include GRPC::Core::CallOps

describe 'ClientStub' do
  let(:noop) { proc { |x| x } }

  before(:each) do
    Thread.abort_on_exception = true
    @server = nil
    @method = 'an_rpc_method'
    @pass = OK
    @fail = INTERNAL
  end

  after(:each) do
    @server.close(from_relative_time(2)) unless @server.nil?
  end

  describe '#new' do
    let(:fake_host) { 'localhost:0' }
    it 'can be created from a host and args' do
      opts = { channel_args: { a_channel_arg: 'an_arg' } }
      blk = proc do
        GRPC::ClientStub.new(fake_host, :this_channel_is_insecure, **opts)
      end
      expect(&blk).not_to raise_error
    end

    it 'can be created with an channel override' do
      opts = {
        channel_args: { a_channel_arg: 'an_arg' },
        channel_override: @ch
      }
      blk = proc do
        GRPC::ClientStub.new(fake_host, :this_channel_is_insecure, **opts)
      end
      expect(&blk).not_to raise_error
    end

    it 'cannot be created with a bad channel override' do
      blk = proc do
        opts = {
          channel_args: { a_channel_arg: 'an_arg' },
          channel_override: Object.new
        }
        GRPC::ClientStub.new(fake_host, :this_channel_is_insecure, **opts)
      end
      expect(&blk).to raise_error
    end

    it 'cannot be created with bad credentials' do
      blk = proc do
        opts = { channel_args: { a_channel_arg: 'an_arg' } }
        GRPC::ClientStub.new(fake_host, Object.new, **opts)
      end
      expect(&blk).to raise_error
    end

    it 'can be created with test test credentials' do
      certs = load_test_certs
      blk = proc do
        opts = {
          channel_args: {
            GRPC::Core::Channel::SSL_TARGET => 'foo.test.google.fr',
            a_channel_arg: 'an_arg'
          }
        }
        creds = GRPC::Core::ChannelCredentials.new(certs[0], nil, nil)
        GRPC::ClientStub.new(fake_host, creds,  **opts)
      end
      expect(&blk).to_not raise_error
    end
  end

  describe '#request_response' do
    before(:each) do
      @sent_msg, @resp = 'a_msg', 'a_reply'
    end

    shared_examples 'request response' do
      it 'should send a request to/receive a reply from a server' do
        server_port = create_test_server
        th = run_request_response(@sent_msg, @resp, @pass)
        stub = GRPC::ClientStub.new("localhost:#{server_port}",
                                    :this_channel_is_insecure)
        expect(get_response(stub)).to eq(@resp)
        th.join
      end

      it 'should send metadata to the server ok' do
        server_port = create_test_server
        host = "localhost:#{server_port}"
        th = run_request_response(@sent_msg, @resp, @pass,
                                  k1: 'v1', k2: 'v2')
        stub = GRPC::ClientStub.new(host, :this_channel_is_insecure)
        expect(get_response(stub)).to eq(@resp)
        th.join
      end

      it 'should send a request when configured using an override channel' do
        server_port = create_test_server
        alt_host = "localhost:#{server_port}"
        th = run_request_response(@sent_msg, @resp, @pass)
        ch = GRPC::Core::Channel.new(alt_host, nil, :this_channel_is_insecure)
        stub = GRPC::ClientStub.new('ignored-host',
                                    :this_channel_is_insecure,
                                    channel_override: ch)
        expect(get_response(stub)).to eq(@resp)
        th.join
      end

      it 'should raise an error if the status is not OK' do
        server_port = create_test_server
        host = "localhost:#{server_port}"
        th = run_request_response(@sent_msg, @resp, @fail)
        stub = GRPC::ClientStub.new(host, :this_channel_is_insecure)
        blk = proc { get_response(stub) }
        expect(&blk).to raise_error(GRPC::BadStatus)
        th.join
      end

      it 'should receive UNAUTHENTICATED if call credentials plugin fails' do
        server_port = create_secure_test_server
        th = run_request_response(@sent_msg, @resp, @pass)

        certs = load_test_certs
        secure_channel_creds = GRPC::Core::ChannelCredentials.new(
          certs[0], nil, nil)
        secure_stub_opts = {
          channel_args: {
            GRPC::Core::Channel::SSL_TARGET => 'foo.test.google.fr'
          }
        }
        stub = GRPC::ClientStub.new("localhost:#{server_port}",
                                    secure_channel_creds, **secure_stub_opts)

        error_message = 'Failing call credentials callback'
        failing_auth = proc do
          fail error_message
        end
        creds = GRPC::Core::CallCredentials.new(failing_auth)

        unauth_error_occured = false
        begin
          get_response(stub, credentials: creds)
        rescue GRPC::Unauthenticated => e
          unauth_error_occured = true
          expect(e.details.include?(error_message)).to be true
        end
        expect(unauth_error_occured).to eq(true)

        # Kill the server thread so tests can complete
        th.kill
      end
    end

    describe 'without a call operation' do
      def get_response(stub, credentials: nil)
        puts credentials.inspect
        stub.request_response(@method, @sent_msg, noop, noop,
                              metadata: { k1: 'v1', k2: 'v2' },
                              credentials: credentials)
      end

      it_behaves_like 'request response'
    end

    describe 'via a call operation' do
      def get_response(stub, run_start_call_first: false, credentials: nil)
        op = stub.request_response(@method, @sent_msg, noop, noop,
                                   return_op: true,
                                   metadata: { k1: 'v1', k2: 'v2' },
                                   deadline: from_relative_time(2),
                                   credentials: credentials)
        expect(op).to be_a(GRPC::ActiveCall::Operation)
        op.start_call if run_start_call_first
        result = op.execute
        op.wait # make sure wait doesn't hang
        result
      end

      it_behaves_like 'request response'

      it 'sends metadata to the server ok when running start_call first' do
        server_port = create_test_server
        host = "localhost:#{server_port}"
        th = run_request_response(@sent_msg, @resp, @pass,
                                  k1: 'v1', k2: 'v2')
        stub = GRPC::ClientStub.new(host, :this_channel_is_insecure)
        expect(get_response(stub)).to eq(@resp)
        th.join
      end
    end
  end

  describe '#client_streamer' do
    before(:each) do
      Thread.abort_on_exception = true
      server_port = create_test_server
      host = "localhost:#{server_port}"
      @stub = GRPC::ClientStub.new(host, :this_channel_is_insecure)
      @metadata = { k1: 'v1', k2: 'v2' }
      @sent_msgs = Array.new(3) { |i| 'msg_' + (i + 1).to_s }
      @resp = 'a_reply'
    end

    shared_examples 'client streaming' do
      it 'should send requests to/receive a reply from a server' do
        th = run_client_streamer(@sent_msgs, @resp, @pass)
        expect(get_response(@stub)).to eq(@resp)
        th.join
      end

      it 'should send metadata to the server ok' do
        th = run_client_streamer(@sent_msgs, @resp, @pass, **@metadata)
        expect(get_response(@stub)).to eq(@resp)
        th.join
      end

      it 'should raise an error if the status is not ok' do
        th = run_client_streamer(@sent_msgs, @resp, @fail)
        blk = proc { get_response(@stub) }
        expect(&blk).to raise_error(GRPC::BadStatus)
        th.join
      end

      it 'should raise ArgumentError if metadata contains invalid values' do
        @metadata.merge!(k3: 3)
        expect do
          get_response(@stub)
        end.to raise_error(ArgumentError,
                           /Header values must be of type string or array/)
      end
    end

    describe 'without a call operation' do
      def get_response(stub)
        stub.client_streamer(@method, @sent_msgs, noop, noop,
                             metadata: @metadata)
      end

      it_behaves_like 'client streaming'
    end

    describe 'via a call operation' do
      def get_response(stub, run_start_call_first: false)
        op = stub.client_streamer(@method, @sent_msgs, noop, noop,
                                  return_op: true, metadata: @metadata)
        expect(op).to be_a(GRPC::ActiveCall::Operation)
        op.start_call if run_start_call_first
        result = op.execute
        op.wait # make sure wait doesn't hang
        result
      end

      it_behaves_like 'client streaming'

      it 'sends metadata to the server ok when running start_call first' do
        th = run_client_streamer(@sent_msgs, @resp, @pass, **@metadata)
        expect(get_response(@stub, run_start_call_first: true)).to eq(@resp)
        th.join
      end
    end
  end

  describe '#server_streamer' do
    before(:each) do
      @sent_msg = 'a_msg'
      @replys = Array.new(3) { |i| 'reply_' + (i + 1).to_s }
    end

    shared_examples 'server streaming' do
      it 'should send a request to/receive replies from a server' do
        server_port = create_test_server
        host = "localhost:#{server_port}"
        th = run_server_streamer(@sent_msg, @replys, @pass)
        stub = GRPC::ClientStub.new(host, :this_channel_is_insecure)
        expect(get_responses(stub).collect { |r| r }).to eq(@replys)
        th.join
      end

      it 'should raise an error if the status is not ok' do
        server_port = create_test_server
        host = "localhost:#{server_port}"
        th = run_server_streamer(@sent_msg, @replys, @fail)
        stub = GRPC::ClientStub.new(host, :this_channel_is_insecure)
        e = get_responses(stub)
        expect { e.collect { |r| r } }.to raise_error(GRPC::BadStatus)
        th.join
      end

      it 'should send metadata to the server ok' do
        server_port = create_test_server
        host = "localhost:#{server_port}"
        th = run_server_streamer(@sent_msg, @replys, @fail,
                                 k1: 'v1', k2: 'v2')
        stub = GRPC::ClientStub.new(host, :this_channel_is_insecure)
        e = get_responses(stub)
        expect { e.collect { |r| r } }.to raise_error(GRPC::BadStatus)
        th.join
      end
    end

    describe 'without a call operation' do
      def get_responses(stub)
        e = stub.server_streamer(@method, @sent_msg, noop, noop,
                                 metadata: { k1: 'v1', k2: 'v2' })
        expect(e).to be_a(Enumerator)
        e
      end

      it_behaves_like 'server streaming'
    end

    describe 'via a call operation' do
      after(:each) do
        @op.wait # make sure wait doesn't hang
      end
      def get_responses(stub, run_start_call_first: false)
        @op = stub.server_streamer(@method, @sent_msg, noop, noop,
                                   return_op: true,
                                   metadata: { k1: 'v1', k2: 'v2' })
        expect(@op).to be_a(GRPC::ActiveCall::Operation)
        @op.start_call if run_start_call_first
        e = @op.execute
        expect(e).to be_a(Enumerator)
        e
      end

      it_behaves_like 'server streaming'

      it 'should send metadata to the server ok when start_call is run first' do
        server_port = create_test_server
        host = "localhost:#{server_port}"
        th = run_server_streamer(@sent_msg, @replys, @fail,
                                 k1: 'v1', k2: 'v2')
        stub = GRPC::ClientStub.new(host, :this_channel_is_insecure)
        e = get_responses(stub, run_start_call_first: true)
        expect { e.collect { |r| r } }.to raise_error(GRPC::BadStatus)
        th.join
      end
    end
  end

  describe '#bidi_streamer' do
    before(:each) do
      @sent_msgs = Array.new(3) { |i| 'msg_' + (i + 1).to_s }
      @replys = Array.new(3) { |i| 'reply_' + (i + 1).to_s }
      server_port = create_test_server
      @host = "localhost:#{server_port}"
    end

    shared_examples 'bidi streaming' do
      it 'supports sending all the requests first', bidi: true do
        th = run_bidi_streamer_handle_inputs_first(@sent_msgs, @replys,
                                                   @pass)
        stub = GRPC::ClientStub.new(@host, :this_channel_is_insecure)
        e = get_responses(stub)
        expect(e.collect { |r| r }).to eq(@replys)
        th.join
      end

      it 'supports client-initiated ping pong', bidi: true do
        th = run_bidi_streamer_echo_ping_pong(@sent_msgs, @pass, true)
        stub = GRPC::ClientStub.new(@host, :this_channel_is_insecure)
        e = get_responses(stub)
        expect(e.collect { |r| r }).to eq(@sent_msgs)
        th.join
      end

      it 'supports a server-initiated ping pong', bidi: true do
        th = run_bidi_streamer_echo_ping_pong(@sent_msgs, @pass, false)
        stub = GRPC::ClientStub.new(@host, :this_channel_is_insecure)
        e = get_responses(stub)
        expect(e.collect { |r| r }).to eq(@sent_msgs)
        th.join
      end
    end

    describe 'without a call operation' do
      def get_responses(stub)
        e = stub.bidi_streamer(@method, @sent_msgs, noop, noop)
        expect(e).to be_a(Enumerator)
        e
      end

      it_behaves_like 'bidi streaming'
    end

    describe 'via a call operation' do
      after(:each) do
        @op.wait # make sure wait doesn't hang
      end
      def get_responses(stub, run_start_call_first: false)
        @op = stub.bidi_streamer(@method, @sent_msgs, noop, noop,
                                 return_op: true)
        expect(@op).to be_a(GRPC::ActiveCall::Operation)
        @op.start_call if run_start_call_first
        e = @op.execute
        expect(e).to be_a(Enumerator)
        e
      end

      it_behaves_like 'bidi streaming'

      it 'can run start_call before executing the call' do
        th = run_bidi_streamer_handle_inputs_first(@sent_msgs, @replys,
                                                   @pass)
        stub = GRPC::ClientStub.new(@host, :this_channel_is_insecure)
        e = get_responses(stub, run_start_call_first: true)
        expect(e.collect { |r| r }).to eq(@replys)
        th.join
      end
    end
  end

  def run_server_streamer(expected_input, replys, status, **kw)
    wanted_metadata = kw.clone
    wakey_thread do |notifier|
      c = expect_server_to_be_invoked(notifier)
      wanted_metadata.each do |k, v|
        expect(c.metadata[k.to_s]).to eq(v)
      end
      expect(c.remote_read).to eq(expected_input)
      replys.each { |r| c.remote_send(r) }
      c.send_status(status, status == @pass ? 'OK' : 'NOK', true)
    end
  end

  def run_bidi_streamer_handle_inputs_first(expected_inputs, replys,
                                            status)
    wakey_thread do |notifier|
      c = expect_server_to_be_invoked(notifier)
      expected_inputs.each { |i| expect(c.remote_read).to eq(i) }
      replys.each { |r| c.remote_send(r) }
      c.send_status(status, status == @pass ? 'OK' : 'NOK', true)
    end
  end

  def run_bidi_streamer_echo_ping_pong(expected_inputs, status, client_starts)
    wakey_thread do |notifier|
      c = expect_server_to_be_invoked(notifier)
      expected_inputs.each do |i|
        if client_starts
          expect(c.remote_read).to eq(i)
          c.remote_send(i)
        else
          c.remote_send(i)
          expect(c.remote_read).to eq(i)
        end
      end
      c.send_status(status, status == @pass ? 'OK' : 'NOK', true)
    end
  end

  def run_client_streamer(expected_inputs, resp, status, **kw)
    wanted_metadata = kw.clone
    wakey_thread do |notifier|
      c = expect_server_to_be_invoked(notifier)
      expected_inputs.each { |i| expect(c.remote_read).to eq(i) }
      wanted_metadata.each do |k, v|
        expect(c.metadata[k.to_s]).to eq(v)
      end
      c.remote_send(resp)
      c.send_status(status, status == @pass ? 'OK' : 'NOK', true)
    end
  end

  def run_request_response(expected_input, resp, status, **kw)
    wanted_metadata = kw.clone
    wakey_thread do |notifier|
      c = expect_server_to_be_invoked(notifier)
      expect(c.remote_read).to eq(expected_input)
      wanted_metadata.each do |k, v|
        expect(c.metadata[k.to_s]).to eq(v)
      end
      c.remote_send(resp)
      c.send_status(status, status == @pass ? 'OK' : 'NOK', true)
    end
  end

  def create_secure_test_server
    certs = load_test_certs
    secure_credentials = GRPC::Core::ServerCredentials.new(
      nil, [{ private_key: certs[1], cert_chain: certs[2] }], false)

    @server = GRPC::Core::Server.new(nil)
    @server.add_http2_port('0.0.0.0:0', secure_credentials)
  end

  def create_test_server
    @server = GRPC::Core::Server.new(nil)
    @server.add_http2_port('0.0.0.0:0', :this_port_is_insecure)
  end

  def expect_server_to_be_invoked(notifier)
    @server.start
    notifier.notify(nil)
    recvd_rpc = @server.request_call
    recvd_call = recvd_rpc.call
    recvd_call.metadata = recvd_rpc.metadata
    recvd_call.run_batch(SEND_INITIAL_METADATA => nil)
    GRPC::ActiveCall.new(recvd_call, noop, noop, INFINITE_FUTURE,
                         metadata_received: true)
  end
end
