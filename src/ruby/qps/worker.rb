#!/usr/bin/env ruby

# Copyright 2016 gRPC authors.
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
require 'src/proto/grpc/testing/worker_service_services_pb'

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
      bms.stop
      q.push(self)
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
      client.shutdown
      q.push(self)
    }
    q.each_item
  end
  def core_count(_args, _call)
    Grpc::Testing::CoreResponse.new(cores: cpu_cores)
  end
  def quit_worker(_args, _call)
    @shutdown_thread = Thread.new {
      @server.stop
    }
    Grpc::Testing::Void.new
  end
  def initialize(s, sp)
    @server = s
    @server_port = sp
  end
  def join_shutdown_thread
    @shutdown_thread.join
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

  # Configure any errors with client or server child threads to surface
  Thread.abort_on_exception = true
  
  s = GRPC::RpcServer.new(poll_period: 3)
  s.add_http2_port("0.0.0.0:" + options['driver_port'].to_s,
                   :this_port_is_insecure)
  worker_service = WorkerServiceImpl.new(s, options['server_port'].to_i)
  s.handle(worker_service)
  s.run
  worker_service.join_shutdown_thread
end

main
