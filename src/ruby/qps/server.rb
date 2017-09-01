#!/usr/bin/env ruby

# Copyright 2016 gRPC authors.
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

# Worker and worker service implementation

this_dir = File.expand_path(File.dirname(__FILE__))
lib_dir = File.join(File.dirname(this_dir), 'lib')
$LOAD_PATH.unshift(lib_dir) unless $LOAD_PATH.include?(lib_dir)
$LOAD_PATH.unshift(this_dir) unless $LOAD_PATH.include?(this_dir)

require 'grpc'
require 'qps-common'
require 'src/proto/grpc/testing/messages_pb'
require 'src/proto/grpc/testing/services_services_pb'
require 'src/proto/grpc/testing/stats_pb'

class BenchmarkServiceImpl < Grpc::Testing::BenchmarkService::Service
  def unary_call(req, _call)
    sr = Grpc::Testing::SimpleResponse
    pl = Grpc::Testing::Payload
    sr.new(payload: pl.new(body: nulls(req.response_size)))
  end
  def streaming_call(reqs)
    PingPongEnumerator.new(reqs).each_item
  end
end

class BenchmarkServer
  def initialize(config, port)
    if config.security_params
      certs = load_test_certs
      cred = GRPC::Core::ServerCredentials.new(
        nil, [{private_key: certs[1], cert_chain: certs[2]}], false)
    else
      cred = :this_port_is_insecure
    end
    # Make sure server can handle the large number of calls in benchmarks
    # TODO: @apolcyn, if scenario config increases total outstanding
    # calls then will need to increase the pool size too
    @server = GRPC::RpcServer.new(pool_size: 1024, max_waiting_requests: 1024)
    @port = @server.add_http2_port("0.0.0.0:" + port.to_s, cred)
    @server.handle(BenchmarkServiceImpl.new)
    @start_time = Time.now
    t = Thread.new {
      @server.run
    }
    t.abort_on_exception
  end
  def mark(reset)
    s = Grpc::Testing::ServerStats.new(time_elapsed:
                                       (Time.now-@start_time).to_f)
    @start_time = Time.now if reset
    s
  end
  def get_port
    @port
  end
  def stop
    @server.stop
  end
end
