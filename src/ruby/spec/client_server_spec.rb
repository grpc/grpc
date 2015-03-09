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
require 'spec_helper'

include GRPC::Core::CompletionType
include GRPC::Core

def load_test_certs
  test_root = File.join(File.dirname(__FILE__), 'testdata')
  files = ['ca.pem', 'server1.key', 'server1.pem']
  files.map { |f| File.open(File.join(test_root, f)).read }
end

shared_context 'setup: tags' do
  before(:example) do
    @server_finished_tag = Object.new
    @client_finished_tag = Object.new
    @client_metadata_tag = Object.new
    @server_tag = Object.new
    @tag = Object.new
  end

  def deadline
    Time.now + 2
  end

  def expect_next_event_on(queue, type, tag)
    ev = queue.pluck(tag, deadline)
    if type.nil?
      expect(ev).to be_nil
    else
      expect(ev).to_not be_nil
      expect(ev.type).to be(type)
    end
    ev
  end

  def server_allows_client_to_proceed
    @server.request_call(@server_tag)
    ev = @server_queue.pluck(@server_tag, deadline)
    expect(ev).not_to be_nil
    expect(ev.type).to be(SERVER_RPC_NEW)
    server_call = ev.call
    server_call.server_accept(@server_queue, @server_finished_tag)
    server_call.server_end_initial_metadata
    server_call
  end

  def server_responds_with(server_call, reply_text)
    reply = ByteBuffer.new(reply_text)
    server_call.start_read(@server_tag)
    ev = @server_queue.pluck(@server_tag, TimeConsts::INFINITE_FUTURE)
    expect(ev.type).to be(READ)
    server_call.start_write(reply, @server_tag)
    ev = @server_queue.pluck(@server_tag, TimeConsts::INFINITE_FUTURE)
    expect(ev).not_to be_nil
    expect(ev.type).to be(WRITE_ACCEPTED)
  end

  def client_sends(call, sent = 'a message')
    req = ByteBuffer.new(sent)
    call.start_write(req, @tag)
    ev = @client_queue.pluck(@tag, TimeConsts::INFINITE_FUTURE)
    expect(ev).not_to be_nil
    expect(ev.type).to be(WRITE_ACCEPTED)
    sent
  end

  def new_client_call
    @ch.create_call('/method', 'foo.test.google.fr', deadline)
  end
end

shared_examples 'basic GRPC message delivery is OK' do
  include_context 'setup: tags'

  it 'servers receive requests from clients and start responding' do
    reply = ByteBuffer.new('the server payload')
    call = new_client_call
    call.invoke(@client_queue, @client_metadata_tag, @client_finished_tag)

    # check the server rpc new was received
    # @server.request_call(@server_tag)
    # ev = expect_next_event_on(@server_queue, SERVER_RPC_NEW, @server_tag)

    # accept the call
    # server_call = ev.call
    # server_call.server_accept(@server_queue, @server_finished_tag)
    # server_call.server_end_initial_metadata
    server_call = server_allows_client_to_proceed

    # client sends a message
    msg = client_sends(call)

    # confirm the server can read the inbound message
    server_call.start_read(@server_tag)
    ev = expect_next_event_on(@server_queue, READ, @server_tag)
    expect(ev.result.to_s).to eq(msg)

    #  the server response
    server_call.start_write(reply, @server_tag)
    expect_next_event_on(@server_queue, WRITE_ACCEPTED, @server_tag)
  end

  it 'responses written by servers are received by the client' do
    call = new_client_call
    call.invoke(@client_queue, @client_metadata_tag, @client_finished_tag)
    server_call = server_allows_client_to_proceed
    client_sends(call)
    server_responds_with(server_call, 'server_response')

    call.start_read(@tag)
    ev = expect_next_event_on(@client_queue, READ, @tag)
    expect(ev.result.to_s).to eq('server_response')
  end

  it 'servers can ignore a client write and send a status' do
    call = new_client_call
    call.invoke(@client_queue, @client_metadata_tag, @client_finished_tag)

    # check the server rpc new was received
    @server.request_call(@server_tag)
    ev = expect_next_event_on(@server_queue, SERVER_RPC_NEW, @server_tag)
    expect(ev.tag).to be(@server_tag)

    # accept the call - need to do this to sent status.
    server_call = ev.call
    server_call.server_accept(@server_queue, @server_finished_tag)
    server_call.server_end_initial_metadata
    server_call.start_write_status(StatusCodes::NOT_FOUND, 'not found',
                                   @server_tag)

    # Client sends some data
    client_sends(call)

    # client gets an empty response for the read, preceeded by some metadata.
    call.start_read(@tag)
    expect_next_event_on(@client_queue, CLIENT_METADATA_READ,
                         @client_metadata_tag)
    ev = expect_next_event_on(@client_queue, READ, @tag)
    expect(ev.tag).to be(@tag)
    expect(ev.result.to_s).to eq('')

    # finally, after client sends writes_done, they get the finished.
    call.writes_done(@tag)
    expect_next_event_on(@client_queue, FINISH_ACCEPTED, @tag)
    ev = expect_next_event_on(@client_queue, FINISHED, @client_finished_tag)
    expect(ev.result.code).to eq(StatusCodes::NOT_FOUND)
  end

  it 'completes calls by sending status to client and server' do
    call = new_client_call
    call.invoke(@client_queue, @client_metadata_tag, @client_finished_tag)
    server_call = server_allows_client_to_proceed
    client_sends(call)
    server_responds_with(server_call, 'server_response')
    server_call.start_write_status(10_101, 'status code is 10101', @server_tag)

    # first the client says writes are done
    call.start_read(@tag)
    expect_next_event_on(@client_queue, READ, @tag)
    call.writes_done(@tag)

    # but nothing happens until the server sends a status
    expect_next_event_on(@server_queue, FINISH_ACCEPTED, @server_tag)
    ev = expect_next_event_on(@server_queue, FINISHED, @server_finished_tag)
    expect(ev.result).to be_a(Struct::Status)

    # client gets FINISHED
    expect_next_event_on(@client_queue, FINISH_ACCEPTED, @tag)
    ev = expect_next_event_on(@client_queue, FINISHED, @client_finished_tag)
    expect(ev.result.details).to eq('status code is 10101')
    expect(ev.result.code).to eq(10_101)
  end
end

shared_examples 'GRPC metadata delivery works OK' do
  include_context 'setup: tags'

  describe 'from client => server' do
    before(:example) do
      n = 7  # arbitrary number of metadata
      diff_keys_fn = proc { |i| [sprintf('k%d', i), sprintf('v%d', i)] }
      diff_keys = Hash[n.times.collect { |x| diff_keys_fn.call x }]
      null_vals_fn = proc { |i| [sprintf('k%d', i), sprintf('v\0%d', i)] }
      null_vals = Hash[n.times.collect { |x| null_vals_fn.call x }]
      same_keys_fn = proc { |i| [sprintf('k%d', i), [sprintf('v%d', i)] * n] }
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
        expect { call.add_metadata(md) }.to raise_error
      end
    end

    it 'sends all the metadata pairs when keys and values are valid' do
      @valid_metadata.each do |md|
        call = new_client_call
        call.add_metadata(md)

        # Client begins a call OK
        call.invoke(@client_queue, @client_metadata_tag, @client_finished_tag)

        # ... server has all metadata available even though the client did not
        # send a write
        @server.request_call(@server_tag)
        ev = expect_next_event_on(@server_queue, SERVER_RPC_NEW, @server_tag)
        replace_symbols = Hash[md.each_pair.collect { |x, y| [x.to_s, y] }]
        result = ev.result.metadata
        expect(result.merge(replace_symbols)).to eq(result)
      end
    end
  end

  describe 'from server => client' do
    before(:example) do
      n = 7  # arbitrary number of metadata
      diff_keys_fn = proc { |i| [sprintf('k%d', i), sprintf('v%d', i)] }
      diff_keys = Hash[n.times.collect { |x| diff_keys_fn.call x }]
      null_vals_fn = proc { |i| [sprintf('k%d', i), sprintf('v\0%d', i)] }
      null_vals = Hash[n.times.collect { |x| null_vals_fn.call x }]
      same_keys_fn = proc { |i| [sprintf('k%d', i), [sprintf('v%d', i)] * n] }
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
        call.invoke(@client_queue, @client_metadata_tag, @client_finished_tag)

        # server gets the invocation
        @server.request_call(@server_tag)
        ev = expect_next_event_on(@server_queue, SERVER_RPC_NEW, @server_tag)
        expect { ev.call.add_metadata(md) }.to raise_error
      end
    end

    it 'sends a hash that contains the status when no metadata is added' do
      call = new_client_call
      call.invoke(@client_queue, @client_metadata_tag, @client_finished_tag)

      # server gets the invocation
      @server.request_call(@server_tag)
      ev = expect_next_event_on(@server_queue, SERVER_RPC_NEW, @server_tag)
      server_call = ev.call

      # ... server accepts the call without adding metadata
      server_call.server_accept(@server_queue, @server_finished_tag)
      server_call.server_end_initial_metadata

      # there is the HTTP status metadata, though there should not be any
      # TODO: update this with the bug number to be resolved
      ev = expect_next_event_on(@client_queue, CLIENT_METADATA_READ,
                                @client_metadata_tag)
      expect(ev.result).to eq({})
    end

    it 'sends all the pairs when keys and values are valid' do
      @valid_metadata.each do |md|
        call = new_client_call
        call.invoke(@client_queue, @client_metadata_tag, @client_finished_tag)

        # server gets the invocation
        @server.request_call(@server_tag)
        ev = expect_next_event_on(@server_queue, SERVER_RPC_NEW, @server_tag)
        server_call = ev.call

        # ... server adds metadata and accepts the call
        server_call.add_metadata(md)
        server_call.server_accept(@server_queue, @server_finished_tag)
        server_call.server_end_initial_metadata

        # Now the client can read the metadata
        ev = expect_next_event_on(@client_queue, CLIENT_METADATA_READ,
                                  @client_metadata_tag)
        replace_symbols = Hash[md.each_pair.collect { |x, y| [x.to_s, y] }]
        expect(ev.result).to eq(replace_symbols)
      end
    end
  end
end

describe 'the http client/server' do
  before(:example) do
    server_host = '0.0.0.0:0'
    @client_queue = GRPC::Core::CompletionQueue.new
    @server_queue = GRPC::Core::CompletionQueue.new
    @server = GRPC::Core::Server.new(@server_queue, nil)
    server_port = @server.add_http2_port(server_host)
    @server.start
    @ch = Channel.new("0.0.0.0:#{server_port}", nil)
  end

  after(:example) do
    @ch.close
    @server.close
  end

  it_behaves_like 'basic GRPC message delivery is OK' do
  end

  it_behaves_like 'GRPC metadata delivery works OK' do
  end
end

describe 'the secure http client/server' do
  before(:example) do
    certs = load_test_certs
    server_host = '0.0.0.0:0'
    @client_queue = GRPC::Core::CompletionQueue.new
    @server_queue = GRPC::Core::CompletionQueue.new
    server_creds = GRPC::Core::ServerCredentials.new(nil, certs[1], certs[2])
    @server = GRPC::Core::Server.new(@server_queue, nil)
    server_port = @server.add_http2_port(server_host, server_creds)
    @server.start
    args = { Channel::SSL_TARGET => 'foo.test.google.fr' }
    @ch = Channel.new("0.0.0.0:#{server_port}", args,
                      GRPC::Core::Credentials.new(certs[0], nil, nil))
  end

  after(:example) do
    @server.close
  end

  it_behaves_like 'basic GRPC message delivery is OK' do
  end

  it_behaves_like 'GRPC metadata delivery works OK' do
  end
end
