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
require 'optparse'
require 'histogram'
require 'etc'
require 'facter'
require 'client'
require 'qps-common'
require 'server'
require 'src/proto/grpc/testing/services_services'

class WorkerServiceImpl < Grpc::Testing::WorkerService::Service
  def cpu_cores
    Facter.value('processors')['count']
  end
  def run_server(reqs)
    q = EnumeratorQueue.new(self)
    Thread.new {
      bms = ''
      gtss = Grpc::Testing::ServerStatus
      reqs.each do |req|
        case req.argtype.to_s
        when 'setup'
          bms = BenchmarkServer.new(req.setup, @server_port)
          q.push(gtss.new(stats: bms.mark(false), port: bms.get_port))
        when 'mark'
          q.push(gtss.new(stats: bms.mark(req.mark.reset), cores: cpu_cores))
        end
      end
      q.push(self)
      bms.stop
    }
    q.each_item
  end
  def run_client(reqs)
    q = EnumeratorQueue.new(self)
    Thread.new {
      client = ''
      reqs.each do |req|
        case req.argtype.to_s
        when 'setup'
          client = BenchmarkClient.new(req.setup)
          q.push(Grpc::Testing::ClientStatus.new(stats: client.mark(false)))
        when 'mark'
          q.push(Grpc::Testing::ClientStatus.new(stats:
                                                   client.mark(req.mark.reset)))
        end
      end
      q.push(self)
      client.shutdown
    }
    q.each_item
  end
  def core_count(_args, _call)
    Grpc::Testing::CoreResponse.new(cores: cpu_cores)
  end
  def quit_worker(_args, _call)
    Thread.new {
      sleep 3
      @server.stop
    }
    Grpc::Testing::Void.new
  end
  def initialize(s, sp)
    @server = s
    @server_port = sp
  end
end

def main
  options = {
    'driver_port' => 0,
    'server_port' => 0
  }
  OptionParser.new do |opts|
    opts.banner = 'Usage: [--driver_port <port>] [--server_port <port>]'
    opts.on('--driver_port PORT', '<port>') do |v|
      options['driver_port'] = v
    end
    opts.on('--server_port PORT', '<port>') do |v|
      options['server_port'] = v
    end
  end.parse!
  s = GRPC::RpcServer.new
  s.add_http2_port("0.0.0.0:" + options['driver_port'].to_s,
                   :this_port_is_insecure)
  s.handle(WorkerServiceImpl.new(s, options['server_port'].to_i))
  s.run
end

main
