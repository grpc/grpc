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

include GRPC::Core::StatusCodes

describe GRPC::ActiveCall do
  ActiveCall = GRPC::ActiveCall
  Call = GRPC::Core::Call
  CallOps = GRPC::Core::CallOps
  WriteFlags = GRPC::Core::WriteFlags

  before(:each) do
    @pass_through = proc { |x| x }
    @server_tag = Object.new
    @tag = Object.new

    @client_queue = GRPC::Core::CompletionQueue.new
    @server_queue = GRPC::Core::CompletionQueue.new
    host = '0.0.0.0:0'
    @server = GRPC::Core::Server.new(@server_queue, nil)
    server_port = @server.add_http2_port(host, :this_port_is_insecure)
    @server.start
    @ch = GRPC::Core::Channel.new("0.0.0.0:#{server_port}", nil,
                                  :this_channel_is_insecure)
  end

  after(:each) do
    @server.close(@server_queue, deadline)
  end

  describe 'restricted view methods' do
    before(:each) do
      call = make_test_call
      md_tag = ActiveCall.client_invoke(call, @client_queue)
      @client_call = ActiveCall.new(call, @client_queue, @pass_through,
                                    @pass_through, deadline,
                                    metadata_tag: md_tag)
    end

    describe '#multi_req_view' do
      it 'exposes a fixed subset of the ActiveCall methods' do
        want = %w(cancelled, deadline, each_remote_read, metadata, shutdown)
        v = @client_call.multi_req_view
        want.each do |w|
          expect(v.methods.include?(w))
        end
      end
    end

    describe '#single_req_view' do
      it 'exposes a fixed subset of the ActiveCall methods' do
        want = %w(cancelled, deadline, metadata, shutdown)
        v = @client_call.single_req_view
        want.each do |w|
          expect(v.methods.include?(w))
        end
      end
    end
  end

  describe '#remote_send' do
    it 'allows a client to send a payload to the server' do
      call = make_test_call
      md_tag = ActiveCall.client_invoke(call, @client_queue)
      @client_call = ActiveCall.new(call, @client_queue, @pass_through,
                                    @pass_through, deadline,
                                    metadata_tag: md_tag)
      msg = 'message is a string'
      @client_call.remote_send(msg)

      # check that server rpc new was received
      recvd_rpc = @server.request_call(@server_queue, @server_tag, deadline)
      expect(recvd_rpc).to_not eq nil
      recvd_call = recvd_rpc.call

      # Accept the call, and verify that the server reads the response ok.
      server_ops = {
        CallOps::SEND_INITIAL_METADATA => {}
      }
      recvd_call.run_batch(@server_queue, @server_tag, deadline, server_ops)
      server_call = ActiveCall.new(recvd_call, @server_queue, @pass_through,
                                   @pass_through, deadline)
      expect(server_call.remote_read).to eq(msg)
    end

    it 'marshals the payload using the marshal func' do
      call = make_test_call
      ActiveCall.client_invoke(call, @client_queue)
      marshal = proc { |x| 'marshalled:' + x }
      client_call = ActiveCall.new(call, @client_queue, marshal,
                                   @pass_through, deadline)
      msg = 'message is a string'
      client_call.remote_send(msg)

      # confirm that the message was marshalled
      recvd_rpc =  @server.request_call(@server_queue, @server_tag, deadline)
      recvd_call = recvd_rpc.call
      server_ops = {
        CallOps::SEND_INITIAL_METADATA => nil
      }
      recvd_call.run_batch(@server_queue, @server_tag, deadline, server_ops)
      server_call = ActiveCall.new(recvd_call, @server_queue, @pass_through,
                                   @pass_through, deadline)
      expect(server_call.remote_read).to eq('marshalled:' + msg)
    end

    TEST_WRITE_FLAGS = [WriteFlags::BUFFER_HINT, WriteFlags::NO_COMPRESS]
    TEST_WRITE_FLAGS.each do |f|
      it "successfully makes calls with write_flag set to #{f}" do
        call = make_test_call
        ActiveCall.client_invoke(call, @client_queue)
        marshal = proc { |x| 'marshalled:' + x }
        client_call = ActiveCall.new(call, @client_queue, marshal,
                                     @pass_through, deadline)
        msg = 'message is a string'
        client_call.write_flag = f
        client_call.remote_send(msg)

        # confirm that the message was marshalled
        recvd_rpc =  @server.request_call(@server_queue, @server_tag, deadline)
        recvd_call = recvd_rpc.call
        server_ops = {
          CallOps::SEND_INITIAL_METADATA => nil
        }
        recvd_call.run_batch(@server_queue, @server_tag, deadline, server_ops)
        server_call = ActiveCall.new(recvd_call, @server_queue, @pass_through,
                                     @pass_through, deadline)
        expect(server_call.remote_read).to eq('marshalled:' + msg)
      end
    end
  end

  describe '#client_invoke' do
    it 'sends metadata to the server when present' do
      call = make_test_call
      metadata = { k1: 'v1', k2: 'v2' }
      ActiveCall.client_invoke(call, @client_queue, metadata)
      recvd_rpc =  @server.request_call(@server_queue, @server_tag, deadline)
      recvd_call = recvd_rpc.call
      expect(recvd_call).to_not be_nil
      expect(recvd_rpc.metadata).to_not be_nil
      expect(recvd_rpc.metadata['k1']).to eq('v1')
      expect(recvd_rpc.metadata['k2']).to eq('v2')
    end
  end

  describe '#remote_read' do
    it 'reads the response sent by a server' do
      call = make_test_call
      md_tag = ActiveCall.client_invoke(call, @client_queue)
      client_call = ActiveCall.new(call, @client_queue, @pass_through,
                                   @pass_through, deadline,
                                   metadata_tag: md_tag)
      msg = 'message is a string'
      client_call.remote_send(msg)
      server_call = expect_server_to_receive(msg)
      server_call.remote_send('server_response')
      expect(client_call.remote_read).to eq('server_response')
    end

    it 'saves no metadata when the server adds no metadata' do
      call = make_test_call
      md_tag = ActiveCall.client_invoke(call, @client_queue)
      client_call = ActiveCall.new(call, @client_queue, @pass_through,
                                   @pass_through, deadline,
                                   metadata_tag: md_tag)
      msg = 'message is a string'
      client_call.remote_send(msg)
      server_call = expect_server_to_receive(msg)
      server_call.remote_send('ignore me')
      expect(client_call.metadata).to be_nil
      client_call.remote_read
      expect(client_call.metadata).to eq({})
    end

    it 'saves metadata add by the server' do
      call = make_test_call
      md_tag = ActiveCall.client_invoke(call, @client_queue)
      client_call = ActiveCall.new(call, @client_queue, @pass_through,
                                   @pass_through, deadline,
                                   metadata_tag: md_tag)
      msg = 'message is a string'
      client_call.remote_send(msg)
      server_call = expect_server_to_receive(msg, k1: 'v1', k2: 'v2')
      server_call.remote_send('ignore me')
      expect(client_call.metadata).to be_nil
      client_call.remote_read
      expected = { 'k1' => 'v1', 'k2' => 'v2' }
      expect(client_call.metadata).to eq(expected)
    end

    it 'get a nil msg before a status when an OK status is sent' do
      call = make_test_call
      md_tag = ActiveCall.client_invoke(call, @client_queue)
      client_call = ActiveCall.new(call, @client_queue, @pass_through,
                                   @pass_through, deadline,
                                   metadata_tag: md_tag)
      msg = 'message is a string'
      client_call.remote_send(msg)
      client_call.writes_done(false)
      server_call = expect_server_to_receive(msg)
      server_call.remote_send('server_response')
      server_call.send_status(OK, 'OK')
      expect(client_call.remote_read).to eq('server_response')
      res = client_call.remote_read
      expect(res).to be_nil
    end

    it 'unmarshals the response using the unmarshal func' do
      call = make_test_call
      md_tag = ActiveCall.client_invoke(call, @client_queue)
      unmarshal = proc { |x| 'unmarshalled:' + x }
      client_call = ActiveCall.new(call, @client_queue, @pass_through,
                                   unmarshal, deadline,
                                   metadata_tag: md_tag)

      # confirm the client receives the unmarshalled message
      msg = 'message is a string'
      client_call.remote_send(msg)
      server_call = expect_server_to_receive(msg)
      server_call.remote_send('server_response')
      expect(client_call.remote_read).to eq('unmarshalled:server_response')
    end
  end

  describe '#each_remote_read' do
    it 'creates an Enumerator' do
      call = make_test_call
      client_call = ActiveCall.new(call, @client_queue, @pass_through,
                                   @pass_through, deadline)
      expect(client_call.each_remote_read).to be_a(Enumerator)
    end

    it 'the returns an enumerator that can read n responses' do
      call = make_test_call
      md_tag = ActiveCall.client_invoke(call, @client_queue)
      client_call = ActiveCall.new(call, @client_queue, @pass_through,
                                   @pass_through, deadline,
                                   metadata_tag: md_tag)
      msg = 'message is a string'
      reply = 'server_response'
      client_call.remote_send(msg)
      server_call = expect_server_to_receive(msg)
      e = client_call.each_remote_read
      n = 3  # arbitrary value > 1
      n.times do
        server_call.remote_send(reply)
        expect(e.next).to eq(reply)
      end
    end

    it 'the returns an enumerator that stops after an OK Status' do
      call = make_test_call
      md_tag = ActiveCall.client_invoke(call, @client_queue)
      client_call = ActiveCall.new(call, @client_queue, @pass_through,
                                   @pass_through, deadline,
                                   metadata_tag: md_tag)
      msg = 'message is a string'
      reply = 'server_response'
      client_call.remote_send(msg)
      client_call.writes_done(false)
      server_call = expect_server_to_receive(msg)
      e = client_call.each_remote_read
      n = 3 # arbitrary value > 1
      n.times do
        server_call.remote_send(reply)
        expect(e.next).to eq(reply)
      end
      server_call.send_status(OK, 'OK')
      expect { e.next }.to raise_error(StopIteration)
    end
  end

  describe '#writes_done' do
    it 'finishes ok if the server sends a status response' do
      call = make_test_call
      md_tag = ActiveCall.client_invoke(call, @client_queue)
      client_call = ActiveCall.new(call, @client_queue, @pass_through,
                                   @pass_through, deadline,
                                   metadata_tag: md_tag)
      msg = 'message is a string'
      client_call.remote_send(msg)
      expect { client_call.writes_done(false) }.to_not raise_error
      server_call = expect_server_to_receive(msg)
      server_call.remote_send('server_response')
      expect(client_call.remote_read).to eq('server_response')
      server_call.send_status(OK, 'status code is OK')
      expect { client_call.finished }.to_not raise_error
    end

    it 'finishes ok if the server sends an early status response' do
      call = make_test_call
      md_tag = ActiveCall.client_invoke(call, @client_queue)
      client_call = ActiveCall.new(call, @client_queue, @pass_through,
                                   @pass_through, deadline,
                                   metadata_tag: md_tag)
      msg = 'message is a string'
      client_call.remote_send(msg)
      server_call = expect_server_to_receive(msg)
      server_call.remote_send('server_response')
      server_call.send_status(OK, 'status code is OK')
      expect(client_call.remote_read).to eq('server_response')
      expect { client_call.writes_done(false) }.to_not raise_error
      expect { client_call.finished }.to_not raise_error
    end

    it 'finishes ok if writes_done is true' do
      call = make_test_call
      md_tag = ActiveCall.client_invoke(call, @client_queue)
      client_call = ActiveCall.new(call, @client_queue, @pass_through,
                                   @pass_through, deadline,
                                   metadata_tag: md_tag)
      msg = 'message is a string'
      client_call.remote_send(msg)
      server_call = expect_server_to_receive(msg)
      server_call.remote_send('server_response')
      server_call.send_status(OK, 'status code is OK')
      expect(client_call.remote_read).to eq('server_response')
      expect { client_call.writes_done(true) }.to_not raise_error
    end
  end

  def expect_server_to_receive(sent_text, **kw)
    c = expect_server_to_be_invoked(**kw)
    expect(c.remote_read).to eq(sent_text)
    c
  end

  def expect_server_to_be_invoked(**kw)
    recvd_rpc =  @server.request_call(@server_queue, @server_tag, deadline)
    expect(recvd_rpc).to_not eq nil
    recvd_call = recvd_rpc.call
    recvd_call.run_batch(@server_queue, @server_tag, deadline,
                         CallOps::SEND_INITIAL_METADATA => kw)
    ActiveCall.new(recvd_call, @server_queue, @pass_through,
                   @pass_through, deadline)
  end

  def make_test_call
    @ch.create_call(@client_queue, nil, nil, '/method', nil, deadline)
  end

  def deadline
    Time.now + 2  # in 2 seconds; arbitrary
  end
end
