#!/usr/bin/env ruby

# Copyright 2014, Google Inc.
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

# interop_client is a testing tool that accesses a gRPC interop testing
# server and runs a test on it.
#
# Helps validate interoperation b/w different gRPC implementations.
#
# Usage: $ path/to/interop_client.rb --server_host=<hostname> \
#                                    --server_port=<port> \
#                                    --test_case=<testcase_name>

this_dir = File.expand_path(File.dirname(__FILE__))
lib_dir = File.join(File.dirname(File.dirname(this_dir)), 'lib')
$LOAD_PATH.unshift(lib_dir) unless $LOAD_PATH.include?(lib_dir)
$LOAD_PATH.unshift(this_dir) unless $LOAD_PATH.include?(this_dir)

require 'optparse'
require 'minitest'
require 'minitest/assertions'

require 'grpc'
require 'google/protobuf'

require 'test/cpp/interop/test_services'
require 'test/cpp/interop/messages'
require 'test/cpp/interop/empty'

# loads the certificates used to access the test server securely.
def load_test_certs
  this_dir = File.expand_path(File.dirname(__FILE__))
  data_dir = File.join(File.dirname(File.dirname(this_dir)), 'spec/testdata')
  files = ['ca.pem', 'server1.key', 'server1.pem']
  files.map { |f| File.open(File.join(data_dir, f)).read }
end

# creates a Credentials from the test certificates.
def test_creds
  certs = load_test_certs
  GRPC::Core::Credentials.new(certs[0])
end

# creates a test stub that accesses host:port securely.
def create_stub(host, port, is_secure)
  address = "#{host}:#{port}"
  if is_secure
    stub_opts = {
      :creds => test_creds,
      GRPC::Core::Channel::SSL_TARGET => 'foo.test.google.com'
    }
    logger.info("... connecting securely to #{address}")
    Grpc::Testing::TestService::Stub.new(address, **stub_opts)
  else
    logger.info("... connecting insecurely to #{address}")
    Grpc::Testing::TestService::Stub.new(address)
  end
end

# produces a string of null chars (\0) of length l.
def nulls(l)
  fail 'requires #{l} to be +ve' if l < 0
  [].pack('x' * l).force_encoding('utf-8')
end

# a PingPongPlayer implements the ping pong bidi test.
class PingPongPlayer
  include Minitest::Assertions
  include Grpc::Testing
  include Grpc::Testing::PayloadType
  attr_accessor :assertions # required by Minitest::Assertions
  attr_accessor :queue

  # reqs is the enumerator over the requests
  def initialize(msg_sizes)
    @queue = Queue.new
    @msg_sizes = msg_sizes
    @assertions = 0  # required by Minitest::Assertions
  end

  def each_item
    return enum_for(:each_item) unless block_given?
    req_cls, p_cls = StreamingOutputCallRequest, ResponseParameters  # short
    count = 0
    @msg_sizes.each do |m|
      req_size, resp_size = m
      req = req_cls.new(payload: Payload.new(body: nulls(req_size)),
                        response_type: :COMPRESSABLE,
                        response_parameters: [p_cls.new(size: resp_size)])
      yield req
      resp = @queue.pop
      assert_equal(:COMPRESSABLE, resp.payload.type,
                   'payload type is wrong')
      assert_equal(resp_size, resp.payload.body.length,
                   'payload body #{i} has the wrong length')
      p "OK: ping_pong #{count}"
      count += 1
    end
  end
end

# defines methods corresponding to each interop test case.
class NamedTests
  include Minitest::Assertions
  include Grpc::Testing
  include Grpc::Testing::PayloadType
  attr_accessor :assertions # required by Minitest::Assertions

  def initialize(stub)
    @assertions = 0  # required by Minitest::Assertions
    @stub = stub
  end

  def empty_unary
    resp = @stub.empty_call(Empty.new)
    assert resp.is_a?(Empty), 'empty_unary: invalid response'
    p 'OK: empty_unary'
  end

  def large_unary
    req_size, wanted_response_size = 271_828, 314_159
    payload = Payload.new(type: :COMPRESSABLE, body: nulls(req_size))
    req = SimpleRequest.new(response_type: :COMPRESSABLE,
                            response_size: wanted_response_size,
                            payload: payload)
    resp = @stub.unary_call(req)
    assert_equal(:COMPRESSABLE, resp.payload.type,
                 'large_unary: payload had the wrong type')
    assert_equal(wanted_response_size, resp.payload.body.length,
                 'large_unary: payload had the wrong length')
    assert_equal(nulls(wanted_response_size), resp.payload.body,
                 'large_unary: payload content is invalid')
    p 'OK: large_unary'
  end

  def client_streaming
    msg_sizes = [27_182, 8, 1828, 45_904]
    wanted_aggregate_size = 74_922
    reqs = msg_sizes.map do |x|
      req = Payload.new(body: nulls(x))
      StreamingInputCallRequest.new(payload: req)
    end
    resp = @stub.streaming_input_call(reqs)
    assert_equal(wanted_aggregate_size, resp.aggregated_payload_size,
                 'client_streaming: aggregate payload size is incorrect')
    p 'OK: client_streaming'
  end

  def server_streaming
    msg_sizes = [31_415, 9, 2653, 58_979]
    response_spec = msg_sizes.map { |s| ResponseParameters.new(size: s) }
    req = StreamingOutputCallRequest.new(response_type: :COMPRESSABLE,
                                         response_parameters: response_spec)
    resps = @stub.streaming_output_call(req)
    resps.each_with_index do |r, i|
      assert i < msg_sizes.length, 'too many responses'
      assert_equal(:COMPRESSABLE, r.payload.type,
                   'payload type is wrong')
      assert_equal(msg_sizes[i], r.payload.body.length,
                   'payload body #{i} has the wrong length')
    end
    p 'OK: server_streaming'
  end

  def ping_pong
    msg_sizes = [[27_182, 31_415], [8, 9], [1828, 2653], [45_904, 58_979]]
    ppp = PingPongPlayer.new(msg_sizes)
    resps = @stub.full_duplex_call(ppp.each_item)
    resps.each { |r| ppp.queue.push(r) }
    p 'OK: ping_pong'
  end

  def all
    all_methods = NamedTests.instance_methods(false).map(&:to_s)
    all_methods.each do |m|
      next if m == 'all' or m.start_with?('assert')
      p "TESTCASE: #{m}"
      self.method(m).call
    end
  end
end

# validates the the command line options, returning them as a Hash.
def parse_options
  options = {
    'secure' => false,
    'server_host' => nil,
    'server_port' => nil,
    'test_case' => nil
  }
  OptionParser.new do |opts|
    opts.banner = 'Usage: --server_host <server_host> --server_port server_port'
    opts.on('--server_host SERVER_HOST', 'server hostname') do |v|
      options['server_host'] = v
    end
    opts.on('--server_port SERVER_PORT', 'server port') do |v|
      options['server_port'] = v
    end
    # instance_methods(false) gives only the methods defined in that class
    test_cases = NamedTests.instance_methods(false).map(&:to_s)
    test_case_list = test_cases.join(',')
    opts.on('--test_case CODE', test_cases, {}, 'select a test_case',
            "  (#{test_case_list})") do |v|
      options['test_case'] = v
    end
    opts.on('-u', '--use_tls', 'access using test creds') do |v|
      options['secure'] = v
    end
  end.parse!

  %w(server_host server_port test_case).each do |arg|
    if options[arg].nil?
      fail(OptionParser::MissingArgument, "please specify --#{arg}")
    end
  end
  options
end

def main
  opts = parse_options
  stub = create_stub(opts['server_host'], opts['server_port'], opts['secure'])
  NamedTests.new(stub).method(opts['test_case']).call
end

main
