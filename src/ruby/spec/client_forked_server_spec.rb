# Copyright 2017, Google Inc.
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
require 'grpc/fork'

# Generic test service
module Echo
  # Generic service
  class Service
    include GRPC::GenericService

    self.marshal_class_method = :try_convert
    self.unmarshal_class_method = :try_convert
    self.service_name = 'EchoTest'

    rpc :Echo, String, String
  end

  Stub = Service.rpc_stub_class
end

# Generic test servicer
class EchoServer < Echo::Service
  def echo(echo_req, _unused_call)
    echo_req
  end
end

describe 'the http client/prefork server' do
  before(:example) do
    server_host = '0.0.0.0:0'
    @server = GRPC::PreforkServer.new
    @server_port = @server.add_http2_port(server_host, :this_port_is_insecure)
    @server.handle(EchoServer)
    @server_thd = Thread.new { @server.run_till_terminated }
  end

  after(:example) do
    @server.stop
    @server_thd.join
  end

  it 'basic GRPC message delivery is OK' do
    @server.wait_till_running
    # Establish 100 seperate connections, a connection is always serviced by
    # the same subprocess
    100.times do |i|
      stub = Echo::Stub.new("0.0.0.0:#{@server_port}",
                            :this_channel_is_insecure,
                            channel_args: { 'channel_idx' => i })
      message = stub.echo('hello', deadline: Time.now + 5)
      expect(message).to eq('hello')
    end
  end
end
