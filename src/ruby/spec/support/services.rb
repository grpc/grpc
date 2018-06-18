# Copyright 2017 gRPC authors.
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

# Test stubs for various scenarios
require 'spec_helper'

# A test message
class EchoMsg
  attr_reader :value

  # @params value [String]
  def initialize(value = 'hello')
    @value = value
  end

  def self.marshal(obj)
    obj.value
  end

  def self.unmarshal(value)
    EchoMsg.new(value)
  end
end

# A test service with an echo implementation.
class EchoService
  include GRPC::GenericService
  rpc :an_rpc, EchoMsg, EchoMsg
  rpc :a_client_streaming_rpc, stream(EchoMsg), EchoMsg
  rpc :a_server_streaming_rpc, EchoMsg, stream(EchoMsg)
  rpc :a_bidi_rpc, stream(EchoMsg), stream(EchoMsg)
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

  def a_client_streaming_rpc(call)
    # iterate through requests so call can complete
    call.output_metadata.update(@trailing_metadata)
    ret = []
    call.each_remote_read.each do |r|
      GRPC.logger.info(r)
      ret << r.value
    end
    EchoMsg.new(ret.join('/'))
  end

  def a_server_streaming_rpc(_req, call)
    call.output_metadata.update(@trailing_metadata)
    [EchoMsg.new('hey1'), EchoMsg.new('hey2')]
  end

  def a_bidi_rpc(requests, call)
    call.output_metadata.update(@trailing_metadata)
    requests.map do |r|
      GRPC.logger.info(r)
      EchoMsg.new("#{r.value}-hey")
    end
  end
end

EchoStub = EchoService.rpc_stub_class

# For testing server interceptors
class TestServerInterceptor < GRPC::ServerInterceptor
  def request_response(request:, call:, method:)
    p "Received request/response call at method #{method}" \
      " with request #{request} for call #{call}"
    call.output_metadata[:interc] = 'from_request_response'
    p "[GRPC::Ok] (#{method.owner.name}.#{method.name})"
    yield
  end

  def client_streamer(call:, method:)
    call.output_metadata[:interc] = 'from_client_streamer'
    # call.each_remote_read.each do |r|
    #   p "In interceptor: #{r}"
    # end
    p "Received client streamer call at method #{method} for call #{call}"
    yield
  end

  def server_streamer(request:, call:, method:)
    p "Received server streamer call at method #{method} with request" \
      " #{request} for call #{call}"
    call.output_metadata[:interc] = 'from_server_streamer'
    yield
  end

  def bidi_streamer(requests:, call:, method:)
    # requests.each do |r|
    #   p "Bidi request: #{r}"
    # end
    p "Received bidi streamer call at method #{method} with requests" \
      " #{requests} for call #{call}"
    call.output_metadata[:interc] = 'from_bidi_streamer'
    yield
  end
end

# For testing client interceptors
class TestClientInterceptor < GRPC::ClientInterceptor
  def request_response(request:, call:, method:, metadata: {})
    p "Intercepted request/response call at method #{method}" \
      " with request #{request} for call #{call}" \
      " and metadata: #{metadata}"
    metadata['foo'] = 'bar_from_request_response'
    yield
  end

  def client_streamer(requests:, call:, method:, metadata: {})
    p "Received client streamer call at method #{method}" \
      " with requests #{requests} for call #{call}" \
      " and metadata: #{metadata}"
    requests.each do |r|
      p "In client interceptor: #{r}"
    end
    metadata['foo'] = 'bar_from_client_streamer'
    yield
  end

  def server_streamer(request:, call:, method:, metadata: {})
    p "Received server streamer call at method #{method}" \
      " with request #{request} for call #{call}" \
      " and metadata: #{metadata}"
    metadata['foo'] = 'bar_from_server_streamer'
    yield
  end

  def bidi_streamer(requests:, call:, method:, metadata: {})
    p "Received bidi streamer call at method #{method}" \
      "with requests #{requests} for call #{call}" \
      " and metadata: #{metadata}"
    requests.each do |r|
      p "In client interceptor: #{r}"
    end
    metadata['foo'] = 'bar_from_bidi_streamer'
    yield
  end
end
