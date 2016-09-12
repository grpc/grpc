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

# Sample gRPC server that implements the Greeter::Helloworld service.
#
# Usage: $ path/to/greeter_server.rb

this_dir = File.expand_path(File.dirname(__FILE__))
lib_dir = File.join(this_dir, '../lib')
$LOAD_PATH.unshift(lib_dir) unless $LOAD_PATH.include?(lib_dir)
$LOAD_PATH.unshift(this_dir)

puts $LOAD_PATH
require 'grpc'
require 'helloworld_services'
require 'ruby-prof'
require 'optparse'

# GreeterServer is simple server that implements the Helloworld Greeter server.
class GreeterServer < Helloworld::Greeter::Service
  # say_hello implements the SayHello rpc method.
  def say_hello(hello_req, _unused_call)
    Helloworld::HelloReply.new(message: "Hello #{hello_req.name}")
  end
end

# main starts an RpcServer that receives requests to GreeterServer at the sample
# server port.
def main
  args = {
    'measurement' => 'CPU_TIME',
    'port' => 50051,
    'host' => 'localhost'
  }

  OptionParser.new do |opts|
    opts.on('-m', '--measurement MEASUREMENT',
            'Profiling Measurement type') { |v| args['measurement'] = v }
    opts.on('-p', '--port PORT',
            'Port number for PORT') { |v| args['port'] = v.to_i }
    opts.on('-h', '--host HOST',
            'Server address') { |v| args['host'] = v }
  end.parse!

  RubyProf.measure_mode = RubyProf.const_get(args['measurement'].upcase)
  RubyProf.start

  Signal.trap("INT") do
    result = RubyProf.stop
    RubyProf::GraphHtmlPrinter.new(result).print(STDOUT, {})
    exit
  end

  s = GRPC::RpcServer.new
  s.add_http2_port("#{args['host']}:#{args['port']}", :this_port_is_insecure)
  s.handle(GreeterServer)
  s.run_till_terminated
end

main
