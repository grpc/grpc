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

def create_channel_creds
  test_root = File.join(File.dirname(__FILE__), 'testdata')
  files = ['ca.pem', 'client.key', 'client.pem']
  creds = files.map { |f| File.open(File.join(test_root, f)).read }
  GRPC::Core::ChannelCredentials.new(creds[0], creds[1], creds[2])
end

def client_cert
  test_root = File.join(File.dirname(__FILE__), 'testdata')
  cert = File.open(File.join(test_root, 'client.pem')).read
  fail unless cert.is_a?(String)
  cert
end

def create_server_creds
  test_root = File.join(File.dirname(__FILE__), 'testdata')
  p "test root: #{test_root}"
  files = ['ca.pem', 'server1.key', 'server1.pem']
  creds = files.map { |f| File.open(File.join(test_root, f)).read }
  GRPC::Core::ServerCredentials.new(
    creds[0],
    [{ private_key: creds[1], cert_chain: creds[2] }],
    true) # force client auth
end

# A test message
class EchoMsg
  def self.marshal(_o)
    ''
  end

  def self.unmarshal(_o)
    EchoMsg.new
  end
end

# a test service that checks the cert of its peer
class SslTestService
  include GRPC::GenericService
  rpc :an_rpc, EchoMsg, EchoMsg
  rpc :a_client_streaming_rpc, stream(EchoMsg), EchoMsg
  rpc :a_server_streaming_rpc, EchoMsg, stream(EchoMsg)
  rpc :a_bidi_rpc, stream(EchoMsg), stream(EchoMsg)

  def check_peer_cert(call)
    error_msg = "want:\n#{client_cert}\n\ngot:\n#{call.peer_cert}"
    fail(error_msg) unless call.peer_cert == client_cert
  end

  def an_rpc(req, call)
    check_peer_cert(call)
    req
  end

  def a_client_streaming_rpc(call)
    check_peer_cert(call)
    call.each_remote_read.each { |r| p r }
    EchoMsg.new
  end

  def a_server_streaming_rpc(_, call)
    check_peer_cert(call)
    [EchoMsg.new, EchoMsg.new]
  end

  def a_bidi_rpc(requests, call)
    check_peer_cert(call)
    requests.each { |r| p r }
    [EchoMsg.new, EchoMsg.new]
  end
end

SslTestServiceStub = SslTestService.rpc_stub_class

describe 'client-server auth' do
  RpcServer = GRPC::RpcServer

  before(:all) do
    server_opts = {
      poll_period: 1
    }
    @srv = RpcServer.new(**server_opts)
    port = @srv.add_http2_port('0.0.0.0:0', create_server_creds)
    @srv.handle(SslTestService)
    @srv_thd = Thread.new { @srv.run }
    @srv.wait_till_running

    client_opts = {
      channel_args: {
        GRPC::Core::Channel::SSL_TARGET => 'foo.test.google.fr'
      }
    }
    @stub = SslTestServiceStub.new("localhost:#{port}",
                                   create_channel_creds,
                                   **client_opts)
  end

  after(:all) do
    expect(@srv.stopped?).to be(false)
    @srv.stop
    @srv_thd.join
  end

  it 'client-server auth with unary RPCs' do
    @stub.an_rpc(EchoMsg.new)
  end

  it 'client-server auth with client streaming RPCs' do
    @stub.a_client_streaming_rpc([EchoMsg.new, EchoMsg.new])
  end

  it 'client-server auth with server streaming RPCs' do
    responses = @stub.a_server_streaming_rpc(EchoMsg.new)
    responses.each { |r| p r }
  end

  it 'client-server auth with bidi RPCs' do
    responses = @stub.a_bidi_rpc([EchoMsg.new, EchoMsg.new])
    responses.each { |r| p r }
  end
end
