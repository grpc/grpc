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

# A test message
class EchoMsg
  def self.marshal(_o)
    ''
  end

  def self.unmarshal(_o)
    EchoMsg.new
  end
end

# A test service with an echo implementation.
class EchoService
  include GRPC::GenericService
  rpc :an_rpc, EchoMsg, EchoMsg
  attr_reader :received_md

  def initialize(**kw)
    @trailing_metadata = kw
    @received_md = []
  end

  def an_rpc(req, call)
    GRPC.logger.info('echo service received a request')
    call.output_metadata.update(@trailing_metadata)
    @received_md << call.metadata unless call.metadata.nil?
    req
  end
end

EchoStub = EchoService.rpc_stub_class

def start_server(port = 0)
  @srv = GRPC::RpcServer.new
  server_port = @srv.add_http2_port("localhost:#{port}", :this_port_is_insecure)
  @srv.handle(EchoService)
  @server_thd = Thread.new { @srv.run }
  @srv.wait_till_running
  server_port
end

def stop_server
  expect(@srv.stopped?).to be(false)
  @srv.stop
  @server_thd.join
  expect(@srv.stopped?).to be(true)
end

describe 'channel connection behavior' do
  it 'the client channel handles temporary loss of a transport' do
    port = start_server
    stub = EchoStub.new("localhost:#{port}", :this_channel_is_insecure)
    req = EchoMsg.new
    expect(stub.an_rpc(req)).to be_a(EchoMsg)
    stop_server
    sleep 1
    # TODO(apolcyn) grabbing the same port might fail, is this stable enough?
    start_server(port)
    expect(stub.an_rpc(req)).to be_a(EchoMsg)
    stop_server
  end

  it 'observably connects and reconnects to transient server' \
    ' when using the channel state API' do
    port = start_server
    ch = GRPC::Core::Channel.new("localhost:#{port}", {},
                                 :this_channel_is_insecure)

    expect(ch.connectivity_state).to be(GRPC::Core::ConnectivityStates::IDLE)

    state = ch.connectivity_state(true)

    count = 0
    while count < 20 && state != GRPC::Core::ConnectivityStates::READY
      ch.watch_connectivity_state(state, Time.now + 60)
      state = ch.connectivity_state(true)
      count += 1
    end

    expect(state).to be(GRPC::Core::ConnectivityStates::READY)

    stop_server

    state = ch.connectivity_state

    count = 0
    while count < 20 && state == GRPC::Core::ConnectivityStates::READY
      ch.watch_connectivity_state(state, Time.now + 60)
      state = ch.connectivity_state
      count += 1
    end

    expect(state).to_not be(GRPC::Core::ConnectivityStates::READY)

    start_server(port)

    state = ch.connectivity_state(true)

    count = 0
    while count < 20 && state != GRPC::Core::ConnectivityStates::READY
      ch.watch_connectivity_state(state, Time.now + 60)
      state = ch.connectivity_state(true)
      count += 1
    end

    expect(state).to be(GRPC::Core::ConnectivityStates::READY)

    stop_server
  end
end
