#!/usr/bin/env ruby

# Copyright 2016, Google Inc.
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
