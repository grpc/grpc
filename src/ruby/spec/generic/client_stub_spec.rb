# Copyright 2014, Google Inc.
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
require 'grpc/generic/active_call'
require 'grpc/generic/client_stub'
require 'xray/thread_dump_signal_handler'
require_relative '../port_picker'

NOOP = Proc.new { |x| x }

def wakey_thread(&blk)
  awake_mutex, awake_cond = Mutex.new, ConditionVariable.new
  t = Thread.new do
    blk.call(awake_mutex, awake_cond)
  end
  awake_mutex.synchronize { awake_cond.wait(awake_mutex) }
  t
end

def load_test_certs
  test_root = File.join(File.parent(File.dirname(__FILE__)), 'testdata')
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
      host = new_test_host
      opts = {:a_channel_arg => 'an_arg'}
      blk = Proc.new do
        GRPC::ClientStub.new(host, @cq, **opts)
      end
      expect(&blk).not_to raise_error
    end

    it 'can be created with a default deadline' do
      host = new_test_host
      opts = {:a_channel_arg => 'an_arg', :deadline => 5}
      blk = Proc.new do
        GRPC::ClientStub.new(host, @cq, **opts)
      end
      expect(&blk).not_to raise_error
    end

    it 'can be created with an channel override' do
      host = new_test_host
      opts = {:a_channel_arg => 'an_arg', :channel_override => @ch}
      blk = Proc.new do
        GRPC::ClientStub.new(host, @cq, **opts)
      end
      expect(&blk).not_to raise_error
    end

    it 'cannot be created with a bad channel override' do
      host = new_test_host
      blk = Proc.new do
        opts = {:a_channel_arg => 'an_arg', :channel_override => Object.new}
        GRPC::ClientStub.new(host, @cq, **opts)
      end
      expect(&blk).to raise_error
    end

    it 'cannot be created with bad credentials' do
      host = new_test_host
      blk = Proc.new do
        opts = {:a_channel_arg => 'an_arg', :creds => Object.new}
        GRPC::ClientStub.new(host, @cq, **opts)
      end
      expect(&blk).to raise_error
    end

    it 'can be created with test test credentials' do
      certs = load_test_certs
      host = new_test_host
      blk = Proc.new do
        opts = {
          GRPC::Core::Channel::SSL_TARGET => 'foo.test.google.com',
          :a_channel_arg => 'an_arg',
          :creds => GRPC::Core::Credentials.new(certs[0], nil, nil)
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

    describe 'without a call operation' do

      it 'should send a request to/receive a_reply from a server' do
        host = new_test_host
        th = run_request_response(host, @sent_msg, @resp, @pass)
        stub = GRPC::ClientStub.new(host, @cq)
        resp = stub.request_response(@method, @sent_msg, NOOP, NOOP)
        expect(resp).to eq(@resp)
        th.join
      end

      it 'should send a request when configured using an override channel' do
        alt_host = new_test_host
        th = run_request_response(alt_host, @sent_msg, @resp, @pass)
        ch = GRPC::Core::Channel.new(alt_host, nil)
        stub = GRPC::ClientStub.new('ignored-host', @cq,
                                    channel_override:ch)
        resp = stub.request_response(@method, @sent_msg, NOOP, NOOP)
        expect(resp).to eq(@resp)
        th.join
      end

      it 'should raise an error if the status is not OK' do
        host = new_test_host
        th = run_request_response(host, @sent_msg, @resp, @fail)
        stub = GRPC::ClientStub.new(host, @cq)
        blk = Proc.new do
          stub.request_response(@method, @sent_msg, NOOP, NOOP)
        end
        expect(&blk).to raise_error(GRPC::BadStatus)
        th.join
      end

    end

    describe 'via a call operation' do

      it 'should send a request to/receive a_reply from a server' do
        host = new_test_host
        th = run_request_response(host, @sent_msg, @resp, @pass)
        stub = GRPC::ClientStub.new(host, @cq)
        op = stub.request_response(@method, @sent_msg, NOOP, NOOP,
                                   return_op:true)
        expect(op).to be_a(GRPC::ActiveCall::Operation)
        resp = op.execute()
        expect(resp).to eq(@resp)
        th.join
      end

      it 'should raise an error if the status is not OK' do
        host = new_test_host
        th = run_request_response(host, @sent_msg, @resp, @fail)
        stub = GRPC::ClientStub.new(host, @cq)
        op = stub.request_response(@method, @sent_msg, NOOP, NOOP,
                                   return_op:true)
        expect(op).to be_a(GRPC::ActiveCall::Operation)
        blk = Proc.new do
          op.execute()
        end
        expect(&blk).to raise_error(GRPC::BadStatus)
        th.join
      end

    end

  end

  describe '#client_streamer' do

    before(:each) do
      @sent_msgs = Array.new(3) { |i| 'msg_' + (i+1).to_s }
      @resp = 'a_reply'
    end

    describe 'without a call operation' do

      it 'should send requests to/receive a reply from a server' do
        host = new_test_host
        th = run_client_streamer(host, @sent_msgs, @resp, @pass)
        stub = GRPC::ClientStub.new(host, @cq)
        resp = stub.client_streamer(@method, @sent_msgs, NOOP, NOOP)
        expect(resp).to eq(@resp)
        th.join
      end

      it 'should raise an error if the status is not ok' do
        host = new_test_host
        th = run_client_streamer(host, @sent_msgs, @resp, @fail)
        stub = GRPC::ClientStub.new(host, @cq)
        blk = Proc.new do
          stub.client_streamer(@method, @sent_msgs, NOOP, NOOP)
        end
        expect(&blk).to raise_error(BadStatus)
        th.join
      end

    end

    describe 'via a call operation' do

      it 'should send requests to/receive a reply from a server' do
        host = new_test_host
        th = run_client_streamer(host, @sent_msgs, @resp, @pass)
        stub = GRPC::ClientStub.new(host, @cq)
        op = stub.client_streamer(@method, @sent_msgs, NOOP, NOOP,
                                  return_op:true)
        expect(op).to be_a(GRPC::ActiveCall::Operation)
        resp = op.execute()
        expect(resp).to eq(@resp)
        th.join
      end

      it 'should raise an error if the status is not ok' do
        host = new_test_host
        th = run_client_streamer(host, @sent_msgs, @resp, @fail)
        stub = GRPC::ClientStub.new(host, @cq)
        op = stub.client_streamer(@method, @sent_msgs, NOOP, NOOP,
                                  return_op:true)
        expect(op).to be_a(GRPC::ActiveCall::Operation)
        blk = Proc.new do
          op.execute()
        end
        expect(&blk).to raise_error(BadStatus)
        th.join
      end

    end

  end

  describe '#server_streamer' do

    before(:each) do
      @sent_msg = 'a_msg'
      @replys = Array.new(3) { |i| 'reply_' + (i+1).to_s }
    end

    describe 'without a call operation' do

      it 'should send a request to/receive replies from a server' do
        host = new_test_host
        th = run_server_streamer(host, @sent_msg, @replys, @pass)
        stub = GRPC::ClientStub.new(host, @cq)
        e = stub.server_streamer(@method, @sent_msg, NOOP, NOOP)
        expect(e).to be_a(Enumerator)
        expect(e.collect { |r| r }).to eq(@replys)
        th.join
      end

      it 'should raise an error if the status is not ok' do
        host = new_test_host
        th = run_server_streamer(host, @sent_msg, @replys, @fail)
        stub = GRPC::ClientStub.new(host, @cq)
        e = stub.server_streamer(@method, @sent_msg, NOOP, NOOP)
        expect(e).to be_a(Enumerator)
        expect { e.collect { |r| r } }.to raise_error(BadStatus)
        th.join
      end

    end

    describe 'via a call operation' do

      it 'should send a request to/receive replies from a server' do
        host = new_test_host
        th = run_server_streamer(host, @sent_msg, @replys, @pass)
        stub = GRPC::ClientStub.new(host, @cq)
        op = stub.server_streamer(@method, @sent_msg, NOOP, NOOP,
                                  return_op:true)
        expect(op).to be_a(GRPC::ActiveCall::Operation)
        e = op.execute()
        expect(e).to be_a(Enumerator)
        th.join
      end

      it 'should raise an error if the status is not ok' do
        host = new_test_host
        th = run_server_streamer(host, @sent_msg, @replys, @fail)
        stub = GRPC::ClientStub.new(host, @cq)
        op = stub.server_streamer(@method, @sent_msg, NOOP, NOOP,
                                  return_op:true)
        expect(op).to be_a(GRPC::ActiveCall::Operation)
        e = op.execute()
        expect(e).to be_a(Enumerator)
        expect { e.collect { |r| r } }.to raise_error(BadStatus)
        th.join
      end

    end

  end

  describe '#bidi_streamer' do
    before(:each) do
      @sent_msgs = Array.new(3) { |i| 'msg_' + (i+1).to_s }
      @replys = Array.new(3) { |i| 'reply_' + (i+1).to_s }
    end

    describe 'without a call operation' do

      it 'supports sending all the requests first', :bidi => true do
        host = new_test_host
        th = run_bidi_streamer_handle_inputs_first(host, @sent_msgs, @replys,
                                                   @pass)
        stub = GRPC::ClientStub.new(host, @cq)
        e = stub.bidi_streamer(@method, @sent_msgs, NOOP, NOOP)
        expect(e).to be_a(Enumerator)
        expect(e.collect { |r| r }).to eq(@replys)
        th.join
      end

      it 'supports client-initiated ping pong', :bidi => true do
        host = new_test_host
        th = run_bidi_streamer_echo_ping_pong(host, @sent_msgs, @pass, true)
        stub = GRPC::ClientStub.new(host, @cq)
        e = stub.bidi_streamer(@method, @sent_msgs, NOOP, NOOP)
        expect(e).to be_a(Enumerator)
        expect(e.collect { |r| r }).to eq(@sent_msgs)
        th.join
      end

      # disabled because an unresolved wire-protocol implementation feature
      #
      # - servers should be able initiate messaging, however, as it stand
      # servers don't know if all the client metadata has been sent until
      # they receive a message from the client.  Without receiving all the
      # metadata, the server does not accept the call, so this test hangs.
      xit 'supports a server-initiated ping pong', :bidi => true do
        host = new_test_host
        th = run_bidi_streamer_echo_ping_pong(host, @sent_msgs, @pass, false)
        stub = GRPC::ClientStub.new(host, @cq)
        e = stub.bidi_streamer(@method, @sent_msgs, NOOP, NOOP)
        expect(e).to be_a(Enumerator)
        expect(e.collect { |r| r }).to eq(@sent_msgs)
        th.join
      end

    end

    describe 'via a call operation' do

      it 'supports sending all the requests first', :bidi => true do
        host = new_test_host
        th = run_bidi_streamer_handle_inputs_first(host, @sent_msgs, @replys,
                                                   @pass)
        stub = GRPC::ClientStub.new(host, @cq)
        op = stub.bidi_streamer(@method, @sent_msgs, NOOP, NOOP,
                                return_op:true)
        expect(op).to be_a(GRPC::ActiveCall::Operation)
        e = op.execute
        expect(e).to be_a(Enumerator)
        expect(e.collect { |r| r }).to eq(@replys)
        th.join
      end

      it 'supports client-initiated ping pong', :bidi => true  do
        host = new_test_host
        th = run_bidi_streamer_echo_ping_pong(host, @sent_msgs, @pass, true)
        stub = GRPC::ClientStub.new(host, @cq)
        op = stub.bidi_streamer(@method, @sent_msgs, NOOP, NOOP,
                                return_op:true)
        expect(op).to be_a(GRPC::ActiveCall::Operation)
        e = op.execute
        expect(e).to be_a(Enumerator)
        expect(e.collect { |r| r }).to eq(@sent_msgs)
        th.join
      end

      # disabled because an unresolved wire-protocol implementation feature
      #
      # - servers should be able initiate messaging, however, as it stand
      # servers don't know if all the client metadata has been sent until
      # they receive a message from the client.  Without receiving all the
      # metadata, the server does not accept the call, so this test hangs.
      xit 'supports server-initiated ping pong', :bidi => true do
        th = run_bidi_streamer_echo_ping_pong(host, @sent_msgs, @pass, false)
        stub = GRPC::ClientStub.new(host, @cq)
        op = stub.bidi_streamer(@method, @sent_msgs, NOOP, NOOP,
                                return_op:true)
        expect(op).to be_a(GRPC::ActiveCall::Operation)
        e = op.execute
        expect(e).to be_a(Enumerator)
        expect(e.collect { |r| r }).to eq(@sent_msgs)
        th.join
      end

    end

  end

  def run_server_streamer(hostname, expected_input, replys, status)
    wakey_thread do |mtx, cnd|
      c = expect_server_to_be_invoked(hostname, mtx, cnd)
      expect(c.remote_read).to eq(expected_input)
      replys.each { |r| c.remote_send(r) }
      c.send_status(status, status == @pass ? 'OK' : 'NOK', true)
    end
  end

  def run_bidi_streamer_handle_inputs_first(hostname, expected_inputs, replys,
                                            status)
    wakey_thread do |mtx, cnd|
      c = expect_server_to_be_invoked(hostname, mtx, cnd)
      expected_inputs.each { |i| expect(c.remote_read).to eq(i) }
      replys.each { |r| c.remote_send(r) }
      c.send_status(status, status == @pass ? 'OK' : 'NOK', true)
    end
  end

  def run_bidi_streamer_echo_ping_pong(hostname, expected_inputs, status,
                                       client_starts)
    wakey_thread do |mtx, cnd|
      c = expect_server_to_be_invoked(hostname, mtx, cnd)
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

  def run_client_streamer(hostname, expected_inputs, resp, status)
    wakey_thread do |mtx, cnd|
      c = expect_server_to_be_invoked(hostname, mtx, cnd)
      expected_inputs.each { |i| expect(c.remote_read).to eq(i) }
      c.remote_send(resp)
      c.send_status(status, status == @pass ? 'OK' : 'NOK', true)
    end
  end

  def run_request_response(hostname, expected_input, resp, status)
    wakey_thread do |mtx, cnd|
      c = expect_server_to_be_invoked(hostname, mtx, cnd)
      expect(c.remote_read).to eq(expected_input)
      c.remote_send(resp)
      c.send_status(status, status == @pass ? 'OK' : 'NOK', true)
    end
  end

  def start_test_server(hostname, awake_mutex, awake_cond)
    server_queue = GRPC::Core::CompletionQueue.new
    @server = GRPC::Core::Server.new(server_queue, nil)
    @server.add_http2_port(hostname)
    @server.start
    @server_tag = Object.new
    @server.request_call(@server_tag)
    awake_mutex.synchronize { awake_cond.signal }
    server_queue
  end

  def expect_server_to_be_invoked(hostname, awake_mutex, awake_cond)
    server_queue = start_test_server(hostname, awake_mutex, awake_cond)
    test_deadline = Time.now + 10  # fail tests after 10 seconds
    ev = server_queue.pluck(@server_tag, INFINITE_FUTURE)
    raise OutOfTime if ev.nil?
    finished_tag = Object.new
    ev.call.accept(server_queue, finished_tag)
    GRPC::ActiveCall.new(ev.call, server_queue, NOOP, NOOP, INFINITE_FUTURE,
                         finished_tag: finished_tag)
  end

  def new_test_host
    port = find_unused_tcp_port
    "localhost:#{port}"
  end

end
