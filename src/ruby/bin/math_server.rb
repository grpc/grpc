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

# Sample gRPC Ruby server that implements the Math::Calc service and helps
# validate GRPC::RpcServer as GRPC implementation using proto2 serialization.
#
# Usage: $ path/to/math_server.rb

this_dir = File.expand_path(File.dirname(__FILE__))
lib_dir = File.join(File.dirname(this_dir), 'lib')
$LOAD_PATH.unshift(lib_dir) unless $LOAD_PATH.include?(lib_dir)
$LOAD_PATH.unshift(this_dir) unless $LOAD_PATH.include?(this_dir)

require 'forwardable'
require 'grpc'
require 'logger'
require 'math_services'
require 'optparse'

# RubyLogger defines a logger for gRPC based on the standard ruby logger.
module RubyLogger
  def logger
    LOGGER
  end

  LOGGER = Logger.new(STDOUT)
end

# GRPC is the general RPC module
module GRPC
  # Inject the noop #logger if no module-level logger method has been injected.
  extend RubyLogger
end

# Holds state for a fibonacci series
class Fibber
  def initialize(limit)
    fail "bad limit: got #{limit}, want limit > 0" if limit < 1
    @limit = limit
  end

  def generator
    return enum_for(:generator) unless block_given?
    idx, current, previous = 0, 1, 1
    until idx == @limit
      if idx.zero? || idx == 1
        yield Math::Num.new(num: 1)
        idx += 1
        next
      end
      tmp = current
      current = previous + current
      previous = tmp
      yield Math::Num.new(num: current)
      idx += 1
    end
  end
end

# A EnumeratorQueue wraps a Queue to yield the items added to it.
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

# The Math::Math:: module occurs because the service has the same name as its
# package. That practice should be avoided by defining real services.
class Calculator < Math::Math::Service
  def div(div_args, _call)
    if div_args.divisor.zero?
      # To send non-OK status handlers raise a StatusError with the code and
      # and detail they want sent as a Status.
      fail GRPC::StatusError.new(GRPC::Status::INVALID_ARGUMENT,
                                 'divisor cannot be 0')
    end

    Math::DivReply.new(quotient: div_args.dividend / div_args.divisor,
                       remainder: div_args.dividend % div_args.divisor)
  end

  def sum(call)
    # the requests are accesible as the Enumerator call#each_request
    nums = call.each_remote_read.collect(&:num)
    sum = nums.inject { |s, x| s + x }
    Math::Num.new(num: sum)
  end

  def fib(fib_args, _call)
    if fib_args.limit < 1
      fail StatusError.new(Status::INVALID_ARGUMENT, 'limit must be >= 0')
    end

    # return an Enumerator of Nums
    Fibber.new(fib_args.limit).generator
    # just return the generator, GRPC::GenericServer sends each actual response
  end

  def div_many(requests)
    # requests is an lazy Enumerator of the requests sent by the client.
    q = EnumeratorQueue.new(self)
    t = Thread.new do
      begin
        requests.each do |req|
          GRPC.logger.info("read #{req.inspect}")
          resp = Math::DivReply.new(quotient: req.dividend / req.divisor,
                                    remainder: req.dividend % req.divisor)
          q.push(resp)
          Thread.pass  # let the internal Bidi threads run
        end
        GRPC.logger.info('finished reads')
        q.push(self)
      rescue StandardError => e
        q.push(e)  # share the exception with the enumerator
        raise e
      end
    end
    t.priority = -2  # hint that the div_many thread should not be favoured
    q.each_item
  end
end

def load_test_certs
  this_dir = File.expand_path(File.dirname(__FILE__))
  data_dir = File.join(File.dirname(this_dir), 'spec/testdata')
  files = ['ca.pem', 'server1.key', 'server1.pem']
  files.map { |f| File.open(File.join(data_dir, f)).read }
end

def test_server_creds
  certs = load_test_certs
  GRPC::Core::ServerCredentials.new(
    nil, [{ private_key: certs[1], cert_chain: certs[2] }], false)
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

  s = GRPC::RpcServer.new
  if options['secure']
    s.add_http2_port(options['host'], test_server_creds)
    GRPC.logger.info("... running securely on #{options['host']}")
  else
    s.add_http2_port(options['host'], :this_port_is_insecure)
    GRPC.logger.info("... running insecurely on #{options['host']}")
  end

  s.handle(Calculator)
  s.run_till_terminated
end

main
