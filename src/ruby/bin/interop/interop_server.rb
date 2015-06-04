#!/usr/bin/env ruby

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

# interop_server is a Testing app that runs a gRPC interop testing server.
#
# It helps validate interoperation b/w gRPC in different environments
#
# Helps validate interoperation b/w different gRPC implementations.
#
# Usage: $ path/to/interop_server.rb --port

this_dir = File.expand_path(File.dirname(__FILE__))
lib_dir = File.join(File.dirname(File.dirname(this_dir)), 'lib')
$LOAD_PATH.unshift(lib_dir) unless $LOAD_PATH.include?(lib_dir)
$LOAD_PATH.unshift(this_dir) unless $LOAD_PATH.include?(this_dir)

require 'forwardable'
require 'optparse'

require 'grpc'

require 'test/cpp/interop/test_services'
require 'test/cpp/interop/messages'
require 'test/cpp/interop/empty'

# loads the certificates by the test server.
def load_test_certs
  this_dir = File.expand_path(File.dirname(__FILE__))
  data_dir = File.join(File.dirname(File.dirname(this_dir)), 'spec/testdata')
  files = ['ca.pem', 'server1.key', 'server1.pem']
  files.map { |f| File.open(File.join(data_dir, f)).read }
end

# creates a ServerCredentials from the test certificates.
def test_server_creds
  certs = load_test_certs
  GRPC::Core::ServerCredentials.new(nil, certs[1], certs[2])
end

# produces a string of null chars (\0) of length l.
def nulls(l)
  fail 'requires #{l} to be +ve' if l < 0
  [].pack('x' * l).force_encoding('utf-8')
end

# A EnumeratorQueue wraps a Queue yielding the items added to it via each_item.
class EnumeratorQueue
  extend Forwardable
  def_delegators :@q, :push

  def initialize(sentinel)
    @q = Queue.new
    @sentinel = sentinel
  end

  def each_item
    return enum_for(:each_item) unless block_given?
    loop do
      r = @q.pop
      break if r.equal?(@sentinel)
      fail r if r.is_a? Exception
      yield r
    end
  end
end

# A runnable implementation of the schema-specified testing service, with each
# service method implemented as required by the interop testing spec.
class TestTarget < Grpc::Testing::TestService::Service
  include Grpc::Testing
  include Grpc::Testing::PayloadType

  def empty_call(_empty, _call)
    Empty.new
  end

  def unary_call(simple_req, _call)
    req_size = simple_req.response_size
    SimpleResponse.new(payload: Payload.new(type: :COMPRESSABLE,
                                            body: nulls(req_size)))
  end

  def streaming_input_call(call)
    sizes = call.each_remote_read.map { |x| x.payload.body.length }
    sum = sizes.inject { |s, x| s + x }
    StreamingInputCallResponse.new(aggregated_payload_size: sum)
  end

  def streaming_output_call(req, _call)
    cls = StreamingOutputCallResponse
    req.response_parameters.map do |p|
      cls.new(payload: Payload.new(type: req.response_type,
                                   body: nulls(p.size)))
    end
  end

  def full_duplex_call(reqs)
    # reqs is a lazy Enumerator of the requests sent by the client.
    q = EnumeratorQueue.new(self)
    cls = StreamingOutputCallResponse
    Thread.new do
      begin
        GRPC.logger.info('interop-server: started receiving')
        reqs.each do |req|
          resp_size = req.response_parameters[0].size
          GRPC.logger.info("read a req, response size is #{resp_size}")
          resp = cls.new(payload: Payload.new(type: req.response_type,
                                              body: nulls(resp_size)))
          q.push(resp)
        end
        GRPC.logger.info('interop-server: finished receiving')
        q.push(self)
      rescue StandardError => e
        GRPC.logger.info('interop-server: failed')
        GRPC.logger.warn(e)
        q.push(e)  # share the exception with the enumerator
      end
    end
    q.each_item
  end

  def half_duplex_call(reqs)
    # TODO: update with unique behaviour of the half_duplex_call if that's
    # ever required by any of the tests.
    full_duplex_call(reqs)
  end
end

# validates the the command line options, returning them as a Hash.
def parse_options
  options = {
    'port' => nil,
    'secure' => false
  }
  OptionParser.new do |opts|
    opts.banner = 'Usage: --port port'
    opts.on('--port PORT', 'server port') do |v|
      options['port'] = v
    end
    opts.on('-s', '--use_tls', 'require a secure connection?') do |v|
      options['secure'] = v
    end
  end.parse!

  if options['port'].nil?
    fail(OptionParser::MissingArgument, 'please specify --port')
  end
  options
end

def main
  opts = parse_options
  host = "0.0.0.0:#{opts['port']}"
  s = GRPC::RpcServer.new
  if opts['secure']
    s.add_http2_port(host, test_server_creds)
    GRPC.logger.info("... running securely on #{host}")
  else
    s.add_http2_port(host)
    GRPC.logger.info("... running insecurely on #{host}")
  end
  s.handle(TestTarget)
  s.run_till_terminated
end

main
