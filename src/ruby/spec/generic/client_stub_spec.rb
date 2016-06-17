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

def wakey_thread(&blk)
  n = GRPC::Notifier.new
  t = Thread.new do
    blk.call(n)
  end
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
    @server_queue = nil
    @method = 'an_rpc_method'
    @pass = OK
    @fail = INTERNAL
    @cq = GRPC::Core::CompletionQueue.new
  end

  after(:each) do
    @server.close(@server_queue) unless @server_queue.nil?
  end

  describe '#new' do
    let(:fake_host) { 'localhost:0' }
    it 'can be created from a host and args' do
      opts = { channel_args: { a_channel_arg: 'an_arg' } }
      blk = proc do
        GRPC::ClientStub.new(fake_host, @cq, :this_channel_is_insecure, **opts)
      end
      expect(&blk).not_to raise_error
    end

    it 'can be created with an channel override' do
      opts = {
        channel_args: { a_channel_arg: 'an_arg' },
        channel_override: @ch
      }
      blk = proc do
        GRPC::ClientStub.new(fake_host, @cq, :this_channel_is_insecure, **opts)
      end
      expect(&blk).not_to raise_error
    end

    it 'cannot be created with a bad channel override' do
      blk = proc do
        opts = {
          channel_args: { a_channel_arg: 'an_arg' },
          channel_override: Object.new
        }
        GRPC::ClientStub.new(fake_host, @cq, :this_channel_is_insecure, **opts)
      end
      expect(&blk).to raise_error
    end

    it 'cannot be created with bad credentials' do
      blk = proc do
        opts = { channel_args: { a_channel_arg: 'an_arg' } }
        GRPC::ClientStub.new(fake_host, @cq, Object.new, **opts)
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
        GRPC::ClientStub.new(fake_host, @cq, creds,  **opts)
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
        stub = GRPC::ClientStub.new("localhost:#{server_port}", @cq,
                                    :this_channel_is_insecure)
        expect(get_response(stub)).to eq(@resp)
        th.join
      end

      it 'should send metadata to the server ok' do
        server_port = create_test_server
        host = "localhost:#{server_port}"
        th = run_request_response(@sent_msg, @resp, @pass,
                                  k1: 'v1', k2: 'v2')
        stub = GRPC::ClientStub.new(host, @cq, :this_channel_is_insecure)
        expect(get_response(stub)).to eq(@resp)
        th.join
      end

      it 'should send a request when configured using an override channel' do
        server_port = create_test_server
        alt_host = "localhost:#{server_port}"
        th = run_request_response(@sent_msg, @resp, @pass)
        ch = GRPC::Core::Channel.new(alt_host, nil, :this_channel_is_insecure)
        stub = GRPC::ClientStub.new('ignored-host', @cq,
                                    :this_channel_is_insecure,
                                    channel_override: ch)
        expect(get_response(stub)).to eq(@resp)
        th.join
      end

      it 'should raise an error if the status is not OK' do
        server_port = create_test_server
        host = "localhost:#{server_port}"
        th = run_request_response(@sent_msg, @resp, @fail)
        stub = GRPC::ClientStub.new(host, @cq, :this_channel_is_insecure)
        blk = proc { get_response(stub) }
        expect(&blk).to raise_error(GRPC::BadStatus)
        th.join
      end
    end

    describe 'without a call operation' do
      def get_response(stub)
        stub.request_response(@method, @sent_msg, noop, noop,
                              metadata: { k1: 'v1', k2: 'v2' })
      end

      it_behaves_like 'request response'
    end

    describe 'via a call operation' do
      def get_response(stub)
        op = stub.request_response(@method, @sent_msg, noop, noop,
                                   return_op: true,
                                   metadata: { k1: 'v1', k2: 'v2' })
        expect(op).to be_a(GRPC::ActiveCall::Operation)
        op.execute
      end

      it_behaves_like 'request response'
    end
  end

  describe '#client_streamer' do
    shared_examples 'client streaming' do
      before(:each) do
        server_port = create_test_server
        host = "localhost:#{server_port}"
        @stub = GRPC::ClientStub.new(host, @cq, :this_channel_is_insecure)
        @metadata = { k1: 'v1', k2: 'v2' }
        @sent_msgs = Array.new(3) { |i| 'msg_' + (i + 1).to_s }
        @resp = 'a_reply'
      end

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
      def get_response(stub)
        op = stub.client_streamer(@method, @sent_msgs, noop, noop,
                                  return_op: true, metadata: @metadata)
        expect(op).to be_a(GRPC::ActiveCall::Operation)
        op.execute
      end

      it_behaves_like 'client streaming'
    end
  end

  describe '#server_streamer' do
    shared_examples 'server streaming' do
      before(:each) do
        @sent_msg = 'a_msg'
        @replys = Array.new(3) { |i| 'reply_' + (i + 1).to_s }
      end

      it 'should send a request to/receive replies from a server' do
        server_port = create_test_server
        host = "localhost:#{server_port}"
        th = run_server_streamer(@sent_msg, @replys, @pass)
        stub = GRPC::ClientStub.new(host, @cq, :this_channel_is_insecure)
        expect(get_responses(stub).collect { |r| r }).to eq(@replys)
        th.join
      end

      it 'should raise an error if the status is not ok' do
        server_port = create_test_server
        host = "localhost:#{server_port}"
        th = run_server_streamer(@sent_msg, @replys, @fail)
        stub = GRPC::ClientStub.new(host, @cq, :this_channel_is_insecure)
        e = get_responses(stub)
        expect { e.collect { |r| r } }.to raise_error(GRPC::BadStatus)
        th.join
      end

      it 'should send metadata to the server ok' do
        server_port = create_test_server
        host = "localhost:#{server_port}"
        th = run_server_streamer(@sent_msg, @replys, @fail,
                                 k1: 'v1', k2: 'v2')
        stub = GRPC::ClientStub.new(host, @cq, :this_channel_is_insecure)
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
      def get_responses(stub)
        op = stub.server_streamer(@method, @sent_msg, noop, noop,
                                  return_op: true,
                                  metadata: { k1: 'v1', k2: 'v2' })
        expect(op).to be_a(GRPC::ActiveCall::Operation)
        e = op.execute
        expect(e).to be_a(Enumerator)
        e
      end

      it_behaves_like 'server streaming'
    end
  end

  describe '#bidi_streamer' do
    shared_examples 'bidi streaming' do
      before(:each) do
        @sent_msgs = Array.new(3) { |i| 'msg_' + (i + 1).to_s }
        @replys = Array.new(3) { |i| 'reply_' + (i + 1).to_s }
        server_port = create_test_server
        @host = "localhost:#{server_port}"
      end

      it 'supports sending all the requests first', bidi: true do
        th = run_bidi_streamer_handle_inputs_first(@sent_msgs, @replys,
                                                   @pass)
        stub = GRPC::ClientStub.new(@host, @cq, :this_channel_is_insecure)
        e = get_responses(stub)
        expect(e.collect { |r| r }).to eq(@replys)
        th.join
      end

      it 'supports client-initiated ping pong', bidi: true do
        th = run_bidi_streamer_echo_ping_pong(@sent_msgs, @pass, true)
        stub = GRPC::ClientStub.new(@host, @cq, :this_channel_is_insecure)
        e = get_responses(stub)
        expect(e.collect { |r| r }).to eq(@sent_msgs)
        th.join
      end

      it 'supports a server-initiated ping pong', bidi: true do
        th = run_bidi_streamer_echo_ping_pong(@sent_msgs, @pass, false)
        stub = GRPC::ClientStub.new(@host, @cq, :this_channel_is_insecure)
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
      def get_responses(stub)
        op = stub.bidi_streamer(@method, @sent_msgs, noop, noop,
                                return_op: true)
        expect(op).to be_a(GRPC::ActiveCall::Operation)
        e = op.execute
        expect(e).to be_a(Enumerator)
        e
      end

      it_behaves_like 'bidi streaming'
    end

    describe 'without enough time to run' do
      before(:each) do
        @sent_msgs = Array.new(3) { |i| 'msg_' + (i + 1).to_s }
        @replys = Array.new(3) { |i| 'reply_' + (i + 1).to_s }
        server_port = create_test_server
        @host = "localhost:#{server_port}"
      end

      it 'should fail with DeadlineExceeded', bidi: true do
        @server.start
        stub = GRPC::ClientStub.new(@host, @cq, :this_channel_is_insecure)
        blk = proc do
          e = stub.bidi_streamer(@method, @sent_msgs, noop, noop,
                                 deadline: from_relative_time(0.001))
          e.collect { |r| r }
        end
        expect(&blk).to raise_error GRPC::BadStatus, /Deadline Exceeded/
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

  def create_test_server
    @server_queue = GRPC::Core::CompletionQueue.new
    @server = GRPC::Core::Server.new(@server_queue, nil)
    @server.add_http2_port('0.0.0.0:0', :this_port_is_insecure)
  end

  def expect_server_to_be_invoked(notifier)
    @server.start
    notifier.notify(nil)
    server_tag = Object.new
    recvd_rpc = @server.request_call(@server_queue, server_tag,
                                     INFINITE_FUTURE)
    recvd_call = recvd_rpc.call
    recvd_call.metadata = recvd_rpc.metadata
    recvd_call.run_batch(@server_queue, server_tag, Time.now + 2,
                         SEND_INITIAL_METADATA => nil)
    GRPC::ActiveCall.new(recvd_call, @server_queue, noop, noop, INFINITE_FUTURE)
  end
end
