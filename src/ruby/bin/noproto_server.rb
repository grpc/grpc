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

# Sample app that helps validate RpcServer without protobuf serialization.
#
# Usage: $ path/to/noproto_server.rb

this_dir = File.expand_path(File.dirname(__FILE__))
lib_dir = File.join(File.dirname(this_dir), 'lib')
$LOAD_PATH.unshift(lib_dir) unless $LOAD_PATH.include?(lib_dir)

require 'grpc'
require 'optparse'

# a simple non-protobuf message class.
class NoProtoMsg
  def self.marshal(_o)
    ''
  end

  def self.unmarshal(_o)
    NoProtoMsg.new
  end
end

# service the uses the non-protobuf message class.
class NoProtoService
  include GRPC::GenericService
  rpc :AnRPC, NoProtoMsg, NoProtoMsg
end

# an implementation of the non-protobuf service.
class NoProto < NoProtoService
  def initialize(_default_var = 'ignored')
  end

  def an_rpc(req, _call)
    logger.info('echo service received a request')
    req
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
  GRPC::Core::ServerCredentials.new(nil, certs[1], certs[2])
end

def main
  options = {
    'host' => 'localhost:9090',
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

  if options['secure']
    s = GRPC::RpcServer.new(creds: test_server_creds)
    s.add_http2_port(options['host'], true)
    logger.info("... running securely on #{options['host']}")
  else
    s = GRPC::RpcServer.new
    s.add_http2_port(options['host'])
    logger.info("... running insecurely on #{options['host']}")
  end

  s.handle(NoProto)
  s.run
end

main
