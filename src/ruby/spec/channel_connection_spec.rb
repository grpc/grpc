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
require 'spec_helper'
require 'timeout'

include Timeout
include GRPC::Core

def start_server(port = 0)
  @srv = GRPC::RpcServer.new(pool_size: 1)
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

  it 'concurrent watches on the same channel' do
    timeout(180) do
      port = start_server
      ch = GRPC::Core::Channel.new("localhost:#{port}", {},
                                   :this_channel_is_insecure)
      stop_server

      thds = []
      50.times do
        thds << Thread.new do
          while ch.connectivity_state(true) != ConnectivityStates::READY
            ch.watch_connectivity_state(
              ConnectivityStates::READY, Time.now + 60)
            break
          end
        end
      end

      sleep 0.01

      start_server(port)

      thds.each(&:join)

      stop_server
    end
  end
end
