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

# Sample app that accesses a Calc service running on a Ruby gRPC server and
# helps validate RpcServer as a gRPC server using proto2 serialization.
#
# Usage: $ path/to/math_client.rb

this_dir = File.expand_path(File.dirname(__FILE__))
lib_dir = File.join(File.dirname(this_dir), 'lib')
$LOAD_PATH.unshift(lib_dir) unless $LOAD_PATH.include?(lib_dir)
$LOAD_PATH.unshift(this_dir) unless $LOAD_PATH.include?(this_dir)

require 'grpc'
require 'math_services'
require 'optparse'

include GRPC::Core::TimeConsts

def do_div(stub)
  logger.info('request_response')
  logger.info('----------------')
  req = Math::DivArgs.new(dividend: 7, divisor: 3)
  logger.info("div(7/3): req=#{req.inspect}")
  resp = stub.div(req, INFINITE_FUTURE)
  logger.info("Answer: #{resp.inspect}")
  logger.info('----------------')
end

def do_sum(stub)
  # to make client streaming requests, pass an enumerable of the inputs
  logger.info('client_streamer')
  logger.info('---------------')
  reqs = [1, 2, 3, 4, 5].map { |x| Math::Num.new(num: x) }
  logger.info("sum(1, 2, 3, 4, 5): reqs=#{reqs.inspect}")
  resp = stub.sum(reqs)  # reqs.is_a?(Enumerable)
  logger.info("Answer: #{resp.inspect}")
  logger.info('---------------')
end

def do_fib(stub)
  logger.info('server_streamer')
  logger.info('----------------')
  req = Math::FibArgs.new(limit: 11)
  logger.info("fib(11): req=#{req.inspect}")
  resp = stub.fib(req, INFINITE_FUTURE)
  resp.each do |r|
    logger.info("Answer: #{r.inspect}")
  end
  logger.info('----------------')
end

def do_div_many(stub)
  logger.info('bidi_streamer')
  logger.info('-------------')
  reqs = []
  reqs << Math::DivArgs.new(dividend: 7, divisor: 3)
  reqs << Math::DivArgs.new(dividend: 5, divisor: 2)
  reqs << Math::DivArgs.new(dividend: 7, divisor: 2)
  logger.info("div(7/3), div(5/2), div(7/2): reqs=#{reqs.inspect}")
  resp = stub.div_many(reqs, 10)
  resp.each do |r|
    logger.info("Answer: #{r.inspect}")
  end
  logger.info('----------------')
end

def load_test_certs
  this_dir = File.expand_path(File.dirname(__FILE__))
  data_dir = File.join(File.dirname(this_dir), 'spec/testdata')
  files = ['ca.pem', 'server1.key', 'server1.pem']
  files.map { |f| File.open(File.join(data_dir, f)).read }
end

def test_creds
  certs = load_test_certs
  GRPC::Core::Credentials.new(certs[0])
end

def main
  options = {
    'host' => 'localhost:7071',
    'secure' => false
  }
  OptionParser.new do |opts|
    opts.banner = 'Usage: [--host <hostname>:<port>] [--secure|-s]'
    opts.on('--host HOST', '<hostname>:<port>') do |v|
      options['host'] = v
    end
    opts.on('-s', '--secure', 'access using test creds') do |v|
      options['secure'] = v
    end
  end.parse!

  # The Math::Math:: module occurs because the service has the same name as its
  # package. That practice should be avoided by defining real services.

  p options
  if options['secure']
    stub_opts = {
      :creds => test_creds,
      GRPC::Core::Channel::SSL_TARGET => 'foo.test.google.com'
    }
    p stub_opts
    p options['host']
    stub = Math::Math::Stub.new(options['host'], **stub_opts)
    logger.info("... connecting securely on #{options['host']}")
  else
    stub = Math::Math::Stub.new(options['host'])
    logger.info("... connecting insecurely on #{options['host']}")
  end

  do_div(stub)
  do_sum(stub)
  do_fib(stub)
  do_div_many(stub)
end

main
