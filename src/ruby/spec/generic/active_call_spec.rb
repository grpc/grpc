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
    host = '0.0.0.0:0'
    @server = GRPC::Core::Server.new(nil)
    server_port = @server.add_http2_port(host, :this_port_is_insecure)
    @server.start
    @ch = GRPC::Core::Channel.new("0.0.0.0:#{server_port}", nil,
                                  :this_channel_is_insecure)
  end

  after(:each) do
    @server.close(deadline)
  end

  describe 'restricted view methods' do
    before(:each) do
      call = make_test_call
      ActiveCall.client_invoke(call)
      @client_call = ActiveCall.new(call, @pass_through,
                                    @pass_through, deadline)
    end

    describe '#multi_req_view' do
      it 'exposes a fixed subset of the ActiveCall.methods' do
        want = %w(cancelled?, deadline, each_remote_read, metadata, \
                  shutdown, peer, peer_cert, send_initial_metadata, \
                  initial_metadata_sent)
        v = @client_call.multi_req_view
        want.each do |w|
          expect(v.methods.include?(w))
        end
      end
    end

    describe '#single_req_view' do
      it 'exposes a fixed subset of the ActiveCall.methods' do
        want = %w(cancelled?, deadline, metadata, shutdown, \
                  send_initial_metadata, metadata_to_send, \
                  merge_metadata_to_send, initial_metadata_sent)
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
      ActiveCall.client_invoke(call)
      @client_call = ActiveCall.new(call, @pass_through,
                                    @pass_through, deadline)
      msg = 'message is a string'
      @client_call.remote_send(msg)

      # check that server rpc new was received
      recvd_rpc = @server.request_call
      expect(recvd_rpc).to_not eq nil
      recvd_call = recvd_rpc.call

      # Accept the call, and verify that the server reads the response ok.
      server_call = ActiveCall.new(recvd_call, @pass_through,
                                   @pass_through, deadline,
                                   metadata_received: true)
      expect(server_call.remote_read).to eq(msg)
    end

    it 'marshals the payload using the marshal func' do
      call = make_test_call
      ActiveCall.client_invoke(call)
      marshal = proc { |x| 'marshalled:' + x }
      client_call = ActiveCall.new(call, marshal, @pass_through, deadline)
      msg = 'message is a string'
      client_call.remote_send(msg)

      # confirm that the message was marshalled
      recvd_rpc =  @server.request_call
      recvd_call = recvd_rpc.call
      server_ops = {
        CallOps::SEND_INITIAL_METADATA => nil
      }
      recvd_call.run_batch(server_ops)
      server_call = ActiveCall.new(recvd_call, @pass_through,
                                   @pass_through, deadline,
                                   metadata_received: true)
      expect(server_call.remote_read).to eq('marshalled:' + msg)
    end

    TEST_WRITE_FLAGS = [WriteFlags::BUFFER_HINT, WriteFlags::NO_COMPRESS]
    TEST_WRITE_FLAGS.each do |f|
      it "successfully makes calls with write_flag set to #{f}" do
        call = make_test_call
        ActiveCall.client_invoke(call)
        marshal = proc { |x| 'marshalled:' + x }
        client_call = ActiveCall.new(call, marshal,
                                     @pass_through, deadline)
        msg = 'message is a string'
        client_call.write_flag = f
        client_call.remote_send(msg)
        # flush the message in case writes are set to buffered
        call.run_batch(CallOps::SEND_CLOSE_FROM_CLIENT => nil) if f == 1

        # confirm that the message was marshalled
        recvd_rpc =  @server.request_call
        recvd_call = recvd_rpc.call
        server_ops = {
          CallOps::SEND_INITIAL_METADATA => nil
        }
        recvd_call.run_batch(server_ops)
        server_call = ActiveCall.new(recvd_call, @pass_through,
                                     @pass_through, deadline,
                                     metadata_received: true)
        expect(server_call.remote_read).to eq('marshalled:' + msg)
      end
    end
  end

  describe 'sending initial metadata', send_initial_metadata: true do
    it 'sends metadata before sending a message if it hasnt been sent yet' do
      call = make_test_call
      @client_call = ActiveCall.new(
        call,
        @pass_through,
        @pass_through,
        deadline,
        started: false)

      metadata = { key: 'dummy_val', other: 'other_val' }
      expect(@client_call.metadata_sent).to eq(false)
      @client_call.merge_metadata_to_send(metadata)

      message = 'dummy message'

      expect(call).to(
        receive(:run_batch)
          .with(
            hash_including(
              CallOps::SEND_INITIAL_METADATA => metadata)).once)

      expect(call).to(
        receive(:run_batch).with(hash_including(
                                   CallOps::SEND_MESSAGE => message)).once)
      @client_call.remote_send(message)

      expect(@client_call.metadata_sent).to eq(true)
    end

    it 'doesnt send metadata if it thinks its already been sent' do
      call = make_test_call

      @client_call = ActiveCall.new(call,
                                    @pass_through,
                                    @pass_through,
                                    deadline)

      expect(@client_call.metadata_sent).to eql(true)
      expect(call).to(
        receive(:run_batch).with(hash_including(
                                   CallOps::SEND_INITIAL_METADATA)).never)

      @client_call.remote_send('test message')
    end

    it 'sends metadata if it is explicitly sent and ok to do so' do
      call = make_test_call

      @client_call = ActiveCall.new(call,
                                    @pass_through,
                                    @pass_through,
                                    deadline,
                                    started: false)

      expect(@client_call.metadata_sent).to eql(false)

      metadata = { test_key: 'val' }
      @client_call.merge_metadata_to_send(metadata)
      expect(@client_call.metadata_to_send).to eq(metadata)

      expect(call).to(
        receive(:run_batch).with(hash_including(
                                   CallOps::SEND_INITIAL_METADATA =>
                                     metadata)).once)
      @client_call.send_initial_metadata
    end

    it 'explicit sending does nothing if metadata has already been sent' do
      call = make_test_call

      @client_call = ActiveCall.new(call,
                                    @pass_through,
                                    @pass_through,
                                    deadline)

      expect(@client_call.metadata_sent).to eql(true)

      blk = proc do
        @client_call.send_initial_metadata
      end

      expect { blk.call }.to_not raise_error
    end
  end

  describe '#merge_metadata_to_send', merge_metadata_to_send: true do
    it 'adds to existing metadata when there is existing metadata to send' do
      call = make_test_call
      starting_metadata = {
        k1: 'key1_val',
        k2: 'key2_val',
        k3: 'key3_val'
      }

      @client_call = ActiveCall.new(
        call,
        @pass_through, @pass_through,
        deadline,
        started: false,
        metadata_to_send: starting_metadata)

      expect(@client_call.metadata_to_send).to eq(starting_metadata)

      @client_call.merge_metadata_to_send(
        k3: 'key3_new_val',
        k4: 'key4_val')

      expected_md_to_send = {
        k1: 'key1_val',
        k2: 'key2_val',
        k3: 'key3_new_val',
        k4: 'key4_val' }

      expect(@client_call.metadata_to_send).to eq(expected_md_to_send)

      @client_call.merge_metadata_to_send(k5: 'key5_val')
      expected_md_to_send.merge!(k5: 'key5_val')
      expect(@client_call.metadata_to_send).to eq(expected_md_to_send)
    end

    it 'fails when initial metadata has already been sent' do
      call = make_test_call
      @client_call = ActiveCall.new(
        call,
        @pass_through,
        @pass_through,
        deadline,
        started: true)

      expect(@client_call.metadata_sent).to eq(true)

      blk = proc do
        @client_call.merge_metadata_to_send(k1: 'key1_val')
      end

      expect { blk.call }.to raise_error
    end
  end

  describe '#client_invoke' do
    it 'sends metadata to the server when present' do
      call = make_test_call
      metadata = { k1: 'v1', k2: 'v2' }
      ActiveCall.client_invoke(call, metadata)
      recvd_rpc =  @server.request_call
      recvd_call = recvd_rpc.call
      expect(recvd_call).to_not be_nil
      expect(recvd_rpc.metadata).to_not be_nil
      expect(recvd_rpc.metadata['k1']).to eq('v1')
      expect(recvd_rpc.metadata['k2']).to eq('v2')
    end
  end

  describe '#send_status', send_status: true do
    it 'works when no metadata or messages have been sent yet' do
      call = make_test_call
      ActiveCall.client_invoke(call)

      recvd_rpc = @server.request_call
      server_call = ActiveCall.new(
        recvd_rpc.call,
        @pass_through,
        @pass_through,
        deadline,
        started: false)

      expect(server_call.metadata_sent).to eq(false)
      blk = proc { server_call.send_status(OK) }
      expect { blk.call }.to_not raise_error
    end
  end

  describe '#remote_read', remote_read: true do
    it 'reads the response sent by a server' do
      call = make_test_call
      ActiveCall.client_invoke(call)
      client_call = ActiveCall.new(call, @pass_through,
                                   @pass_through, deadline)
      msg = 'message is a string'
      client_call.remote_send(msg)
      server_call = expect_server_to_receive(msg)
      server_call.remote_send('server_response')
      expect(client_call.remote_read).to eq('server_response')
    end

    it 'saves no metadata when the server adds no metadata' do
      call = make_test_call
      ActiveCall.client_invoke(call)
      client_call = ActiveCall.new(call, @pass_through,
                                   @pass_through, deadline)
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
      ActiveCall.client_invoke(call)
      client_call = ActiveCall.new(call, @pass_through,
                                   @pass_through, deadline)
      msg = 'message is a string'
      client_call.remote_send(msg)
      server_call = expect_server_to_receive(msg, k1: 'v1', k2: 'v2')
      server_call.remote_send('ignore me')
      expect(client_call.metadata).to be_nil
      client_call.remote_read
      expected = { 'k1' => 'v1', 'k2' => 'v2' }
      expect(client_call.metadata).to eq(expected)
    end

    it 'get a status from server when nothing else sent from server' do
      client_call = make_test_call
      ActiveCall.client_invoke(client_call)

      recvd_rpc = @server.request_call
      recvd_call = recvd_rpc.call

      server_call = ActiveCall.new(
        recvd_call,
        @pass_through,
        @pass_through,
        deadline,
        started: false)

      server_call.send_status(OK, 'OK')

      # Check that we can receive initial metadata and a status
      client_call.run_batch(
        CallOps::RECV_INITIAL_METADATA => nil)
      batch_result = client_call.run_batch(
        CallOps::RECV_STATUS_ON_CLIENT => nil)

      expect(batch_result.status.code).to eq(OK)
    end

    it 'get a nil msg before a status when an OK status is sent' do
      call = make_test_call
      ActiveCall.client_invoke(call)
      client_call = ActiveCall.new(call, @pass_through,
                                   @pass_through, deadline)
      msg = 'message is a string'
      client_call.remote_send(msg)
      call.run_batch(CallOps::SEND_CLOSE_FROM_CLIENT => nil)
      server_call = expect_server_to_receive(msg)
      server_call.remote_send('server_response')
      server_call.send_status(OK, 'OK')
      expect(client_call.remote_read).to eq('server_response')
      res = client_call.remote_read
      expect(res).to be_nil
    end

    it 'unmarshals the response using the unmarshal func' do
      call = make_test_call
      ActiveCall.client_invoke(call)
      unmarshal = proc { |x| 'unmarshalled:' + x }
      client_call = ActiveCall.new(call, @pass_through,
                                   unmarshal, deadline)

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
      client_call = ActiveCall.new(call, @pass_through,
                                   @pass_through, deadline)
      expect(client_call.each_remote_read).to be_a(Enumerator)
    end

    it 'the returns an enumerator that can read n responses' do
      call = make_test_call
      ActiveCall.client_invoke(call)
      client_call = ActiveCall.new(call, @pass_through,
                                   @pass_through, deadline)
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
      ActiveCall.client_invoke(call)
      client_call = ActiveCall.new(call, @pass_through,
                                   @pass_through, deadline)
      msg = 'message is a string'
      reply = 'server_response'
      client_call.remote_send(msg)
      call.run_batch(CallOps::SEND_CLOSE_FROM_CLIENT => nil)
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

  describe '#closing the call from the client' do
    it 'finishes ok if the server sends a status response' do
      call = make_test_call
      ActiveCall.client_invoke(call)
      client_call = ActiveCall.new(call, @pass_through,
                                   @pass_through, deadline)
      msg = 'message is a string'
      client_call.remote_send(msg)
      expect do
        call.run_batch(CallOps::SEND_CLOSE_FROM_CLIENT => nil)
      end.to_not raise_error
      server_call = expect_server_to_receive(msg)
      server_call.remote_send('server_response')
      expect(client_call.remote_read).to eq('server_response')
      server_call.send_status(OK, 'status code is OK')
      expect { client_call.finished }.to_not raise_error
    end

    it 'finishes ok if the server sends an early status response' do
      call = make_test_call
      ActiveCall.client_invoke(call)
      client_call = ActiveCall.new(call, @pass_through,
                                   @pass_through, deadline)
      msg = 'message is a string'
      client_call.remote_send(msg)
      server_call = expect_server_to_receive(msg)
      server_call.remote_send('server_response')
      server_call.send_status(OK, 'status code is OK')
      expect(client_call.remote_read).to eq('server_response')
      expect do
        call.run_batch(CallOps::SEND_CLOSE_FROM_CLIENT => nil)
      end.to_not raise_error
      expect { client_call.finished }.to_not raise_error
    end

    it 'finishes ok if SEND_CLOSE and RECV_STATUS has been sent' do
      call = make_test_call
      ActiveCall.client_invoke(call)
      client_call = ActiveCall.new(call, @pass_through,
                                   @pass_through, deadline)
      msg = 'message is a string'
      client_call.remote_send(msg)
      server_call = expect_server_to_receive(msg)
      server_call.remote_send('server_response')
      server_call.send_status(OK, 'status code is OK')
      expect(client_call.remote_read).to eq('server_response')
      expect do
        call.run_batch(
          CallOps::SEND_CLOSE_FROM_CLIENT => nil,
          CallOps::RECV_STATUS_ON_CLIENT => nil)
      end.to_not raise_error
    end
  end

  # Test sending of the initial metadata in #run_server_bidi
  # from the server handler both implicitly and explicitly.
  describe '#run_server_bidi metadata sending tests', run_server_bidi: true do
    before(:each) do
      @requests = ['first message', 'second message']
      @server_to_client_metadata = { 'test_key' => 'test_val' }
      @server_status = OK

      @client_call = make_test_call
      @client_call.run_batch(CallOps::SEND_INITIAL_METADATA => {})

      recvd_rpc = @server.request_call
      recvd_call = recvd_rpc.call
      @server_call = ActiveCall.new(
        recvd_call,
        @pass_through,
        @pass_through,
        deadline,
        metadata_received: true,
        started: false,
        metadata_to_send: @server_to_client_metadata)
    end

    after(:each) do
      # Send the requests and send a close so the server can send a status
      @requests.each do |message|
        @client_call.run_batch(CallOps::SEND_MESSAGE => message)
      end
      @client_call.run_batch(CallOps::SEND_CLOSE_FROM_CLIENT => nil)

      @server_thread.join

      # Expect that initial metadata was sent,
      # the requests were echoed, and a status was sent
      batch_result = @client_call.run_batch(
        CallOps::RECV_INITIAL_METADATA => nil)
      expect(batch_result.metadata).to eq(@server_to_client_metadata)

      @requests.each do |message|
        batch_result = @client_call.run_batch(
          CallOps::RECV_MESSAGE => nil)
        expect(batch_result.message).to eq(message)
      end

      batch_result = @client_call.run_batch(
        CallOps::RECV_STATUS_ON_CLIENT => nil)
      expect(batch_result.status.code).to eq(@server_status)
    end

    it 'sends the initial metadata implicitly if not already sent' do
      # Server handler that doesn't have access to a "call"
      # It echoes the requests
      fake_gen_each_reply_with_no_call_param = proc do |msgs|
        msgs
      end

      @server_thread = Thread.new do
        @server_call.run_server_bidi(
          fake_gen_each_reply_with_no_call_param)
        @server_call.send_status(@server_status)
      end
    end

    it 'sends the metadata when sent explicitly and not already sent' do
      # Fake server handler that has access to a "call" object and
      # uses it to explicitly update and send the initial metadata
      fake_gen_each_reply_with_call_param = proc do |msgs, call_param|
        call_param.merge_metadata_to_send(@server_to_client_metadata)
        call_param.send_initial_metadata
        msgs
      end

      @server_thread = Thread.new do
        @server_call.run_server_bidi(
          fake_gen_each_reply_with_call_param)
        @server_call.send_status(@server_status)
      end
    end
  end

  def expect_server_to_receive(sent_text, **kw)
    c = expect_server_to_be_invoked(**kw)
    expect(c.remote_read).to eq(sent_text)
    c
  end

  def expect_server_to_be_invoked(**kw)
    recvd_rpc =  @server.request_call
    expect(recvd_rpc).to_not eq nil
    recvd_call = recvd_rpc.call
    recvd_call.run_batch(CallOps::SEND_INITIAL_METADATA => kw)
    ActiveCall.new(recvd_call, @pass_through, @pass_through, deadline,
                   metadata_received: true, started: true)
  end

  def make_test_call
    @ch.create_call(nil, nil, '/method', nil, deadline)
  end

  def deadline
    Time.now + 2  # in 2 seconds; arbitrary
  end
end
