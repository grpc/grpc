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
  attr_reader :msg

  def initialize(msg: '')
    @msg = msg
  end

  def self.marshal(o)
    o.msg
  end

  def self.unmarshal(msg)
    EchoMsg.new(msg: msg)
  end
end

# A test service with an echo implementation.
class EchoService
  include GRPC::GenericService
  rpc :an_rpc, EchoMsg, EchoMsg
  rpc :a_client_streaming_rpc, stream(EchoMsg), EchoMsg
  rpc :a_server_streaming_rpc, EchoMsg, stream(EchoMsg)
  rpc :a_bidi_rpc, stream(EchoMsg), stream(EchoMsg)
  rpc :a_client_streaming_rpc_unimplemented, stream(EchoMsg), EchoMsg
  attr_reader :received_md

  def initialize(**kw)
    @trailing_metadata = kw
    @received_md = []
  end

  def an_rpc(req, call)
    GRPC.logger.info('echo service received a request')
    call.metadata_to_send.update(call.metadata)
    call.output_metadata.update(@trailing_metadata)
    @received_md << call.metadata unless call.metadata.nil?
    req
  end

  def a_client_streaming_rpc(call)
    # iterate through requests so call can complete
    call.metadata_to_send.update(call.metadata)
    call.output_metadata.update(@trailing_metadata)
    call.each_remote_read.each do |r|
      GRPC.logger.info(r)
    end
    EchoMsg.new
  end

  def a_server_streaming_rpc(_req, call)
    call.metadata_to_send.update(call.metadata)
    call.output_metadata.update(@trailing_metadata)
    [EchoMsg.new, EchoMsg.new]
  end

  def a_bidi_rpc(requests, call)
    call.metadata_to_send.update(call.metadata)
    call.output_metadata.update(@trailing_metadata)
    requests.each do |r|
      GRPC.logger.info(r)
    end
    [EchoMsg.new, EchoMsg.new]
  end
end

EchoStub = EchoService.rpc_stub_class

# For testing server interceptors
class TestServerInterceptor < GRPC::ServerInterceptor
  attr_reader :request_counts

  def initialize
    @request_counts = {
      request_response: 0,
      client_streamer: 0,
      server_streamer: 0,
      bidi_streamer: 0
    }
  end

  def request_response(request:, call:, method:)
    @request_counts[:request_response] += 1
    GRPC.logger.info("Received request/response call at method #{method}" \
      " with request #{request} for call #{call}")
    call.output_metadata[:interc] = 'from_request_response'
    GRPC.logger.info("[GRPC::Ok] (#{method.owner.name}.#{method.name})")
    yield
  end

  def client_streamer(call:, method:)
    @request_counts[:client_streamer] += 1
    call.output_metadata[:interc] = 'from_client_streamer'
    call.each_remote_read.each do |r|
      GRPC.logger.info("In interceptor: #{r}")
    end
    GRPC.logger.info(
      "Received client streamer call at method #{method} for call #{call}"
    )
    yield
  end

  def server_streamer(request:, call:, method:)
    @request_counts[:server_streamer] += 1
    GRPC.logger.info("Received server streamer call at method #{method} with request" \
      " #{request} for call #{call}")
    call.output_metadata[:interc] = 'from_server_streamer'
    yield
  end

  def bidi_streamer(requests:, call:, method:)
    @request_counts[:bidi_streamer] += 1
    requests.each do |r|
      GRPC.logger.info("Bidi request: #{r}")
    end
    GRPC.logger.info("Received bidi streamer call at method #{method} with requests" \
      " #{requests} for call #{call}")
    call.output_metadata[:interc] = 'from_bidi_streamer'
    yield
  end
end

# For testing client interceptors
class TestClientInterceptor < GRPC::ClientInterceptor
  attr_reader :request_counts

  def initialize
    @request_counts = {
      request_response: 0,
      client_streamer: 0,
      server_streamer: 0,
      bidi_streamer: 0
    }
  end

  def request_response(request:, call:, method:, metadata: {})
    @request_counts[:request_response] += 1
    GRPC.logger.info("Intercepted request/response call at method #{method}" \
      " with request #{request} for call #{call}" \
      " and metadata: #{metadata}")
    metadata['foo'] = 'bar_from_request_response'
    yield
  end

  def client_streamer(requests:, call:, method:, metadata: {})
    @request_counts[:client_streamer] += 1
    GRPC.logger.info("Received client streamer call at method #{method}" \
      " with requests #{requests} for call #{call}" \
      " and metadata: #{metadata}")
    requests.each do |r|
      GRPC.logger.info("In client interceptor: #{r}")
    end
    metadata['foo'] = 'bar_from_client_streamer'
    yield
  end

  def server_streamer(request:, call:, method:, metadata: {})
    @request_counts[:server_streamer] += 1
    GRPC.logger.info("Received server streamer call at method #{method}" \
      " with request #{request} for call #{call}" \
      " and metadata: #{metadata}")
    metadata['foo'] = 'bar_from_server_streamer'
    yield
  end

  def bidi_streamer(requests:, call:, method:, metadata: {})
    @request_counts[:bidi_streamer] += 1
    GRPC.logger.info("Received bidi streamer call at method #{method}" \
      "with requests #{requests} for call #{call}" \
      " and metadata: #{metadata}")
    requests.each do |r|
      GRPC.logger.info("In client interceptor: #{r}")
    end
    metadata['foo'] = 'bar_from_bidi_streamer'
    yield
  end
end
