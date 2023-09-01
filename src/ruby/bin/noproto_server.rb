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
    GRPC.logger.info('echo service received a request')
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
  GRPC::Core::ServerCredentials.new(
    nil, [{ private_key: certs[1], cert_chain: certs[2] }], false)
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

  s = GRPC::RpcServer.new
  if options['secure']
    s.add_http2_port(options['host'], test_server_creds)
    GRPC.logger.info("... running securely on #{options['host']}")
  else
    s.add_http2_port(options['host'], :this_port_is_insecure)
    GRPC.logger.info("... running insecurely on #{options['host']}")
  end

  s.handle(NoProto)
  s.run_till_terminated
end

main
