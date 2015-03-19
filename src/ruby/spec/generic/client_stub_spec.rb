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
require 'xray/thread_dump_signal_handler'

NOOP = proc { |x| x }
FAKE_HOST = 'localhost:0'

def wakey_thread(&blk)
  awake_mutex, awake_cond = Mutex.new, ConditionVariable.new
  t = Thread.new do
    blk.call(awake_mutex, awake_cond)
  end
  awake_mutex.synchronize { awake_cond.wait(awake_mutex) }
  t
end

def load_test_certs
  test_root = File.join(File.dirname(File.dirname(__FILE__)), 'testdata')
  files = ['ca.pem', 'server1.key', 'server1.pem']
  files.map { |f| File.open(File.join(test_root, f)).read }
end

include GRPC::Core::StatusCodes
include GRPC::Core::TimeConsts

describe 'ClientStub' do
  before(:each) do
    Thread.abort_on_exception = true
    @server = nil
    @method = 'an_rpc_method'
    @pass = OK
    @fail = INTERNAL
    @cq = GRPC::Core::CompletionQueue.new
  end

  after(:each) do
    @server.close unless @server.nil?
  end

  describe '#new' do
    it 'can be created from a host and args' do
      host = FAKE_HOST
      opts = { a_channel_arg: 'an_arg' }
      blk = proc do
        GRPC::ClientStub.new(host, @cq, **opts)
      end
      expect(&blk).not_to raise_error
    end

    it 'can be created with a default deadline' do
      host = FAKE_HOST
      opts = { a_channel_arg: 'an_arg', deadline: 5 }
      blk = proc do
        GRPC::ClientStub.new(host, @cq, **opts)
      end
      expect(&blk).not_to raise_error
    end

    it 'can be created with an channel override' do
      host = FAKE_HOST
      opts = { a_channel_arg: 'an_arg', channel_override: @ch }
      blk = proc do
        GRPC::ClientStub.new(host, @cq, **opts)
      end
      expect(&blk).not_to raise_error
    end

    it 'cannot be created with a bad channel override' do
      host = FAKE_HOST
      blk = proc do
        opts = { a_channel_arg: 'an_arg', channel_override: Object.new }
        GRPC::ClientStub.new(host, @cq, **opts)
      end
      expect(&blk).to raise_error
    end

    it 'cannot be created with bad credentials' do
      host = FAKE_HOST
      blk = proc do
        opts = { a_channel_arg: 'an_arg', creds: Object.new }
        GRPC::ClientStub.new(host, @cq, **opts)
      end
      expect(&blk).to raise_error
    end

    it 'can be created with test test credentials' do
      certs = load_test_certs
      host = FAKE_HOST
      blk = proc do
        opts = {
          GRPC::Core::Channel::SSL_TARGET => 'foo.test.google.fr',
          a_channel_arg: 'an_arg',
          creds: GRPC::Core::Credentials.new(certs[0], nil, nil)
        }
        GRPC::ClientStub.new(host, @cq, **opts)
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
        stub = GRPC::ClientStub.new("localhost:#{server_port}", @cq)
        expect(get_response(stub)).to eq(@resp)
        th.join
      end

      it 'should send metadata to the server ok' do
        server_port = create_test_server
        host = "localhost:#{server_port}"
        th = run_request_response(@sent_msg, @resp, @pass,
                                  k1: 'v1', k2: 'v2')
        stub = GRPC::ClientStub.new(host, @cq)
        expect(get_response(stub)).to eq(@resp)
        th.join
      end

      it 'should update the sent metadata with a provided metadata updater' do
        server_port = create_test_server
        host = "localhost:#{server_port}"
        th = run_request_response(@sent_msg, @resp, @pass,
                                  k1: 'updated-v1', k2: 'v2')
        update_md = proc do |md|
          md[:k1] = 'updated-v1'
          md
        end
        stub = GRPC::ClientStub.new(host, @cq, update_metadata: update_md)
        expect(get_response(stub)).to eq(@resp)
        th.join
      end

      it 'should send a request when configured using an override channel' do
        server_port = create_test_server
        alt_host = "localhost:#{server_port}"
        th = run_request_response(@sent_msg, @resp, @pass)
        ch = GRPC::Core::Channel.new(alt_host, nil)
        stub = GRPC::ClientStub.new('ignored-host', @cq, channel_override: ch)
        expect(get_response(stub)).to eq(@resp)
        th.join
      end

      it 'should raise an error if the status is not OK' do
        server_port = create_test_server
        host = "localhost:#{server_port}"
        th = run_request_response(@sent_msg, @resp, @fail)
        stub = GRPC::ClientStub.new(host, @cq)
        blk = proc { get_response(stub) }
        expect(&blk).to raise_error(GRPC::BadStatus)
        th.join
      end
    end

    describe 'without a call operation' do
      def get_response(stub)
        stub.request_response(@method, @sent_msg, NOOP, NOOP,
                              k1: 'v1', k2: 'v2')
      end

      it_behaves_like 'request response'
    end

    describe 'via a call operation' do
      def get_response(stub)
        op = stub.request_response(@method, @sent_msg, NOOP, NOOP,
                                   return_op: true, k1: 'v1', k2: 'v2')
        expect(op).to be_a(GRPC::ActiveCall::Operation)
        op.execute
      end

      it_behaves_like 'request response'
    end
  end

  describe '#client_streamer' do
    shared_examples 'client streaming' do
      before(:each) do
        @sent_msgs = Array.new(3) { |i| 'msg_' + (i + 1).to_s }
        @resp = 'a_reply'
      end

      it 'should send requests to/receive a reply from a server' do
        server_port = create_test_server
        host = "localhost:#{server_port}"
        th = run_client_streamer(@sent_msgs, @resp, @pass)
        stub = GRPC::ClientStub.new(host, @cq)
        expect(get_response(stub)).to eq(@resp)
        th.join
      end

      it 'should send metadata to the server ok' do
        server_port = create_test_server
        host = "localhost:#{server_port}"
        th = run_client_streamer(@sent_msgs, @resp, @pass,
                                 k1: 'v1', k2: 'v2')
        stub = GRPC::ClientStub.new(host, @cq)
        expect(get_response(stub)).to eq(@resp)
        th.join
      end

      it 'should update the sent metadata with a provided metadata updater' do
        server_port = create_test_server
        host = "localhost:#{server_port}"
        th = run_client_streamer(@sent_msgs, @resp, @pass,
                                 k1: 'updated-v1', k2: 'v2')
        update_md = proc do |md|
          md[:k1] = 'updated-v1'
          md
        end
        stub = GRPC::ClientStub.new(host, @cq, update_metadata: update_md)
        expect(get_response(stub)).to eq(@resp)
        th.join
      end

      it 'should raise an error if the status is not ok' do
        server_port = create_test_server
        host = "localhost:#{server_port}"
        th = run_client_streamer(@sent_msgs, @resp, @fail)
        stub = GRPC::ClientStub.new(host, @cq)
        blk = proc { get_response(stub) }
        expect(&blk).to raise_error(GRPC::BadStatus)
        th.join
      end
    end

    describe 'without a call operation' do
      def get_response(stub)
        stub.client_streamer(@method, @sent_msgs, NOOP, NOOP,
                             k1: 'v1', k2: 'v2')
      end

      it_behaves_like 'client streaming'
    end

    describe 'via a call operation' do
      def get_response(stub)
        op = stub.client_streamer(@method, @sent_msgs, NOOP, NOOP,
                                  return_op: true, k1: 'v1', k2: 'v2')
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
        stub = GRPC::ClientStub.new(host, @cq)
        expect(get_responses(stub).collect { |r| r }).to eq(@replys)
        th.join
      end

      it 'should raise an error if the status is not ok' do
        server_port = create_test_server
        host = "localhost:#{server_port}"
        th = run_server_streamer(@sent_msg, @replys, @fail)
        stub = GRPC::ClientStub.new(host, @cq)
        e = get_responses(stub)
        expect { e.collect { |r| r } }.to raise_error(GRPC::BadStatus)
        th.join
      end

      it 'should send metadata to the server ok' do
        server_port = create_test_server
        host = "localhost:#{server_port}"
        th = run_server_streamer(@sent_msg, @replys, @fail,
                                 k1: 'v1', k2: 'v2')
        stub = GRPC::ClientStub.new(host, @cq)
        e = get_responses(stub)
        expect { e.collect { |r| r } }.to raise_error(GRPC::BadStatus)
        th.join
      end

      it 'should update the sent metadata with a provided metadata updater' do
        server_port = create_test_server
        host = "localhost:#{server_port}"
        th = run_server_streamer(@sent_msg, @replys, @pass,
                                 k1: 'updated-v1', k2: 'v2')
        update_md = proc do |md|
          md[:k1] = 'updated-v1'
          md
        end
        stub = GRPC::ClientStub.new(host, @cq, update_metadata: update_md)
        e = get_responses(stub)
        expect(e.collect { |r| r }).to eq(@replys)
        th.join
      end
    end

    describe 'without a call operation' do
      def get_responses(stub)
        e = stub.server_streamer(@method, @sent_msg, NOOP, NOOP,
                                 k1: 'v1', k2: 'v2')
        expect(e).to be_a(Enumerator)
        e
      end

      it_behaves_like 'server streaming'
    end

    describe 'via a call operation' do
      def get_responses(stub)
        op = stub.server_streamer(@method, @sent_msg, NOOP, NOOP,
                                  return_op: true, k1: 'v1', k2: 'v2')
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
      end

      it 'supports sending all the requests first', bidi: true do
        server_port = create_test_server
        host = "localhost:#{server_port}"
        th = run_bidi_streamer_handle_inputs_first(@sent_msgs, @replys,
                                                   @pass)
        stub = GRPC::ClientStub.new(host, @cq)
        e = get_responses(stub)
        expect(e.collect { |r| r }).to eq(@replys)
        th.join
      end

      it 'supports client-initiated ping pong', bidi: true do
        server_port = create_test_server
        host = "localhost:#{server_port}"
        th = run_bidi_streamer_echo_ping_pong(@sent_msgs, @pass, true)
        stub = GRPC::ClientStub.new(host, @cq)
        e = get_responses(stub)
        expect(e.collect { |r| r }).to eq(@sent_msgs)
        th.join
      end

      # disabled because an unresolved wire-protocol implementation feature
      #
      # - servers should be able initiate messaging, however, as it stand
      # servers don't know if all the client metadata has been sent until
      # they receive a message from the client.  Without receiving all the
      # metadata, the server does not accept the call, so this test hangs.
      xit 'supports a server-initiated ping pong', bidi: true do
        server_port = create_test_server
        host = "localhost:#{server_port}"
        th = run_bidi_streamer_echo_ping_pong(@sent_msgs, @pass, false)
        stub = GRPC::ClientStub.new(host, @cq)
        e = get_responses(stub)
        expect(e.collect { |r| r }).to eq(@sent_msgs)
        th.join
      end
    end

    describe 'without a call operation' do
      def get_responses(stub)
        e = stub.bidi_streamer(@method, @sent_msgs, NOOP, NOOP)
        expect(e).to be_a(Enumerator)
        e
      end

      it_behaves_like 'bidi streaming'
    end

    describe 'via a call operation' do
      def get_responses(stub)
        op = stub.bidi_streamer(@method, @sent_msgs, NOOP, NOOP,
                                return_op: true)
        expect(op).to be_a(GRPC::ActiveCall::Operation)
        e = op.execute
        expect(e).to be_a(Enumerator)
        e
      end

      it_behaves_like 'bidi streaming'
    end
  end

  def run_server_streamer(expected_input, replys, status, **kw)
    wanted_metadata = kw.clone
    wakey_thread do |mtx, cnd|
      c = expect_server_to_be_invoked(mtx, cnd)
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
    wakey_thread do |mtx, cnd|
      c = expect_server_to_be_invoked(mtx, cnd)
      expected_inputs.each { |i| expect(c.remote_read).to eq(i) }
      replys.each { |r| c.remote_send(r) }
      c.send_status(status, status == @pass ? 'OK' : 'NOK', true)
    end
  end

  def run_bidi_streamer_echo_ping_pong(expected_inputs, status, client_starts)
    wakey_thread do |mtx, cnd|
      c = expect_server_to_be_invoked(mtx, cnd)
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
    wakey_thread do |mtx, cnd|
      c = expect_server_to_be_invoked(mtx, cnd)
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
    wakey_thread do |mtx, cnd|
      c = expect_server_to_be_invoked(mtx, cnd)
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
    @server.add_http2_port('0.0.0.0:0')
  end

  def start_test_server(awake_mutex, awake_cond)
    @server.start
    @server_tag = Object.new
    @server.request_call(@server_tag)
    awake_mutex.synchronize { awake_cond.signal }
  end

  def expect_server_to_be_invoked(awake_mutex, awake_cond)
    start_test_server(awake_mutex, awake_cond)
    ev = @server_queue.pluck(@server_tag, INFINITE_FUTURE)
    fail OutOfTime if ev.nil?
    server_call = ev.call
    server_call.metadata = ev.result.metadata
    finished_tag = Object.new
    server_call.server_accept(@server_queue, finished_tag)
    server_call.server_end_initial_metadata
    GRPC::ActiveCall.new(server_call, @server_queue, NOOP, NOOP,
                         INFINITE_FUTURE,
                         finished_tag: finished_tag)
  end
end
