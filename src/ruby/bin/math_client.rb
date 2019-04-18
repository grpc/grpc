#!/usr/bin/env ruby

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

# Sample app that accesses a Calc service running on a Ruby gRPC server and
# helps validate RpcServer as a gRPC server using proto2 serialization.
#
# Usage: $ path/to/math_client.rb

this_dir = File.expand_path(File.dirname(__FILE__))
lib_dir = File.join(File.dirname(this_dir), 'lib')
$LOAD_PATH.unshift(lib_dir) unless $LOAD_PATH.include?(lib_dir)
$LOAD_PATH.unshift(this_dir) unless $LOAD_PATH.include?(this_dir)

require 'grpc'
require 'math_services_pb'
require 'optparse'
require 'logger'

include GRPC::Core::TimeConsts

module StdoutLogger
  def logger
    LOGGER
  end

  LOGGER = Logger.new(STDOUT)
end

GRPC.extend(StdoutLogger)

def do_div(stub)
  GRPC.logger.info('request_response')
  GRPC.logger.info('----------------')
  req = Math::DivArgs.new(dividend: 7, divisor: 3)
  GRPC.logger.info("div(7/3): req=#{req.inspect}")
  resp = stub.div(req)
  GRPC.logger.info("Answer: #{resp.inspect}")
  GRPC.logger.info('----------------')
end

def do_sum(stub)
  # to make client streaming requests, pass an enumerable of the inputs
  GRPC.logger.info('client_streamer')
  GRPC.logger.info('---------------')
  reqs = [1, 2, 3, 4, 5].map { |x| Math::Num.new(num: x) }
  GRPC.logger.info("sum(1, 2, 3, 4, 5): reqs=#{reqs.inspect}")
  resp = stub.sum(reqs)  # reqs.is_a?(Enumerable)
  GRPC.logger.info("Answer: #{resp.inspect}")
  GRPC.logger.info('---------------')
end

def do_fib(stub)
  GRPC.logger.info('server_streamer')
  GRPC.logger.info('----------------')
  req = Math::FibArgs.new(limit: 11)
  GRPC.logger.info("fib(11): req=#{req.inspect}")
  resp = stub.fib(req)
  resp.each do |r|
    GRPC.logger.info("Answer: #{r.inspect}")
  end
  GRPC.logger.info('----------------')
end

def do_div_many(stub)
  GRPC.logger.info('bidi_streamer')
  GRPC.logger.info('-------------')
  reqs = []
  reqs << Math::DivArgs.new(dividend: 7, divisor: 3)
  reqs << Math::DivArgs.new(dividend: 5, divisor: 2)
  reqs << Math::DivArgs.new(dividend: 7, divisor: 2)
  GRPC.logger.info("div(7/3), div(5/2), div(7/2): reqs=#{reqs.inspect}")
  resp = stub.div_many(reqs)
  resp.each do |r|
    GRPC.logger.info("Answer: #{r.inspect}")
  end
  GRPC.logger.info('----------------')
end

def load_test_certs
  this_dir = File.expand_path(File.dirname(__FILE__))
  data_dir = File.join(File.dirname(this_dir), 'spec/testdata')
  files = ['ca.pem', 'server1.key', 'server1.pem']
  files.map { |f| File.open(File.join(data_dir, f)).read }
end

def test_creds
  certs = load_test_certs
  GRPC::Core::ChannelCredentials.new(certs[0])
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
  if options['secure']
    stub_opts = {
      :creds => test_creds,
      GRPC::Core::Channel::SSL_TARGET => 'foo.test.google.fr',
      timeout: INFINITE_FUTURE,
    }
    stub = Math::Math::Stub.new(options['host'], **stub_opts)
    GRPC.logger.info("... connecting securely on #{options['host']}")
  else
    stub = Math::Math::Stub.new(options['host'], :this_channel_is_insecure, timeout: INFINITE_FUTURE)
    GRPC.logger.info("... connecting insecurely on #{options['host']}")
  end

  do_div(stub)
  do_sum(stub)
  do_fib(stub)
  do_div_many(stub)
end

main
