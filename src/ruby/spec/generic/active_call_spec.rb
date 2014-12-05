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
require_relative '../port_picker'

ActiveCall = GRPC::ActiveCall

describe GRPC::ActiveCall do

  before(:each) do
    @pass_through = Proc.new { |x| x }
    @server_tag = Object.new
    @server_finished_tag = Object.new
    @tag = Object.new

    @client_queue = GRPC::Core::CompletionQueue.new
    @server_queue = GRPC::Core::CompletionQueue.new
    port = find_unused_tcp_port
    host = "localhost:#{port}"
    @server = GRPC::Core::Server.new(@server_queue, nil)
    @server.add_http2_port(host)
    @server.start
    @ch = GRPC::Core::Channel.new(host, nil)
  end

  after(:each) do
    @server.close
  end

  describe 'restricted view methods' do
    before(:each) do
      call = make_test_call
      finished_tag = ActiveCall.client_start_invoke(call, @client_queue,
                                                    deadline)
      @client_call = ActiveCall.new(call, @client_queue, @pass_through,
                                    @pass_through, deadline,
                                    finished_tag: finished_tag)
    end

    describe '#multi_req_view' do
      it 'exposes a fixed subset of the ActiveCall methods' do
        want = ['cancelled', 'deadline', 'each_remote_read', 'shutdown']
        v = @client_call.multi_req_view
        want.each do |w|
          expect(v.methods.include?(w))
        end
      end
    end

    describe '#single_req_view' do
      it 'exposes a fixed subset of the ActiveCall methods' do
        want = ['cancelled', 'deadline', 'shutdown']
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
      finished_tag = ActiveCall.client_start_invoke(call, @client_queue,
                                                    deadline)
      @client_call = ActiveCall.new(call, @client_queue, @pass_through,
                                    @pass_through, deadline,
                                    finished_tag: finished_tag)
      msg = 'message is a string'
      @client_call.remote_send(msg)

      # check that server rpc new was received
      @server.request_call(@server_tag)
      ev = @server_queue.next(deadline)
      expect(ev.type).to be(CompletionType::SERVER_RPC_NEW)
      expect(ev.call).to be_a(Call)
      expect(ev.tag).to be(@server_tag)

      # Accept the call, and verify that the server reads the response ok.
      ev.call.accept(@client_queue, @server_tag)
      server_call = ActiveCall.new(ev.call, @client_queue, @pass_through,
                                   @pass_through, deadline)
      expect(server_call.remote_read).to eq(msg)
    end

    it 'marshals the payload using the marshal func' do
      call = make_test_call
      finished_tag = ActiveCall.client_start_invoke(call, @client_queue,
                                                    deadline)
      marshal = Proc.new { |x| 'marshalled:' + x }
      client_call = ActiveCall.new(call, @client_queue, marshal,
                                   @pass_through, deadline,
                                   finished_tag: finished_tag)
      msg = 'message is a string'
      client_call.remote_send(msg)

      # confirm that the message was marshalled
      @server.request_call(@server_tag)
      ev = @server_queue.next(deadline)
      ev.call.accept(@client_queue, @server_tag)
      server_call = ActiveCall.new(ev.call, @client_queue, @pass_through,
                                   @pass_through, deadline)
      expect(server_call.remote_read).to eq('marshalled:' + msg)
    end

  end

  describe '#remote_read' do
    it 'reads the response sent by a server' do
      call, pass_through = make_test_call, Proc.new { |x| x }
      finished_tag = ActiveCall.client_start_invoke(call, @client_queue,
                                                    deadline)
      client_call = ActiveCall.new(call, @client_queue, @pass_through,
                                   @pass_through, deadline,
                                   finished_tag: finished_tag)
      msg = 'message is a string'
      client_call.remote_send(msg)
      server_call = expect_server_to_receive(msg)
      server_call.remote_send('server_response')
      expect(client_call.remote_read).to eq('server_response')
    end

    it 'get a nil msg before a status when an OK status is sent' do
      call, pass_through = make_test_call, Proc.new { |x| x }
      finished_tag = ActiveCall.client_start_invoke(call, @client_queue,
                                                    deadline)
      client_call = ActiveCall.new(call, @client_queue, @pass_through,
                                   @pass_through, deadline,
                                   finished_tag: finished_tag)
      msg = 'message is a string'
      client_call.remote_send(msg)
      client_call.writes_done(false)
      server_call = expect_server_to_receive(msg)
      server_call.remote_send('server_response')
      server_call.send_status(StatusCodes::OK, 'OK')
      expect(client_call.remote_read).to eq('server_response')
      res = client_call.remote_read
      expect(res).to be_nil
    end


    it 'unmarshals the response using the unmarshal func' do
      call = make_test_call
      finished_tag = ActiveCall.client_start_invoke(call, @client_queue,
                                                    deadline)
      unmarshal = Proc.new { |x| 'unmarshalled:' + x }
      client_call = ActiveCall.new(call, @client_queue, @pass_through,
                                   unmarshal, deadline,
                                   finished_tag: finished_tag)

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
      finished_tag = ActiveCall.client_start_invoke(call, @client_queue,
                                                    deadline)
      client_call = ActiveCall.new(call, @client_queue, @pass_through,
                                   @pass_through, deadline,
                                   finished_tag: finished_tag)
      msg = 'message is 4a string'
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
      finished_tag = ActiveCall.client_start_invoke(call, @client_queue,
                                                    deadline)
      client_call = ActiveCall.new(call, @client_queue, @pass_through,
                                   @pass_through, deadline,
                                   finished_tag: finished_tag)
      msg = 'message is a string'
      reply = 'server_response'
      client_call.remote_send(msg)
      client_call.writes_done(false)
      server_call = expect_server_to_receive(msg)
      e = client_call.each_remote_read
      n = 3  # arbitrary value > 1
      n.times do
        server_call.remote_send(reply)
        expect(e.next).to eq(reply)
      end
      server_call.send_status(StatusCodes::OK, 'OK')
      expect { e.next }.to raise_error(StopIteration)
    end

  end

  describe '#writes_done' do
    it 'finishes ok if the server sends a status response' do
      call = make_test_call
      finished_tag = ActiveCall.client_start_invoke(call, @client_queue,
                                                    deadline)
      client_call = ActiveCall.new(call, @client_queue, @pass_through,
                                   @pass_through, deadline,
                                   finished_tag: finished_tag)
      msg = 'message is a string'
      client_call.remote_send(msg)
      expect { client_call.writes_done(false) }.to_not raise_error
      server_call = expect_server_to_receive(msg)
      server_call.remote_send('server_response')
      expect(client_call.remote_read).to eq('server_response')
      server_call.send_status(StatusCodes::OK, 'status code is OK')
      expect { server_call.finished }.to_not raise_error
      expect { client_call.finished }.to_not raise_error
    end

    it 'finishes ok if the server sends an early status response' do
      call = make_test_call
      finished_tag = ActiveCall.client_start_invoke(call, @client_queue,
                                                    deadline)
      client_call = ActiveCall.new(call, @client_queue, @pass_through,
                                   @pass_through, deadline,
                                   finished_tag: finished_tag)
      msg = 'message is a string'
      client_call.remote_send(msg)
      server_call = expect_server_to_receive(msg)
      server_call.remote_send('server_response')
      server_call.send_status(StatusCodes::OK, 'status code is OK')
      expect(client_call.remote_read).to eq('server_response')
      expect { client_call.writes_done(false) }.to_not raise_error
      expect { server_call.finished }.to_not raise_error
      expect { client_call.finished }.to_not raise_error
    end

    it 'finishes ok if writes_done is true' do
      call = make_test_call
      finished_tag = ActiveCall.client_start_invoke(call, @client_queue,
                                                    deadline)
      client_call = ActiveCall.new(call, @client_queue, @pass_through,
                                   @pass_through, deadline,
                                   finished_tag: finished_tag)
      msg = 'message is a string'
      client_call.remote_send(msg)
      server_call = expect_server_to_receive(msg)
      server_call.remote_send('server_response')
      server_call.send_status(StatusCodes::OK, 'status code is OK')
      expect(client_call.remote_read).to eq('server_response')
      expect { client_call.writes_done(true) }.to_not raise_error
      expect { server_call.finished }.to_not raise_error
    end

  end

  def expect_server_to_receive(sent_text)
    c = expect_server_to_be_invoked
    expect(c.remote_read).to eq(sent_text)
    c
  end

  def expect_server_to_be_invoked()
    @server.request_call(@server_tag)
    ev = @server_queue.next(deadline)
    ev.call.accept(@client_queue, @server_finished_tag)
    ActiveCall.new(ev.call, @client_queue, @pass_through,
                   @pass_through, deadline,
                   finished_tag: @server_finished_tag)
  end

  def make_test_call
    @ch.create_call('dummy_method', 'dummy_host', deadline)
  end

  def deadline
    Time.now + 0.25  # in 0.25 seconds; arbitrary
  end

end
