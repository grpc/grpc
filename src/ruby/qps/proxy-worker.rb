#!/usr/bin/env ruby

# Copyright 2017 gRPC authors.
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

# Proxy of worker service implementation for running a PHP client

this_dir = File.expand_path(File.dirname(__FILE__))
lib_dir = File.join(File.dirname(this_dir), 'lib')
$LOAD_PATH.unshift(lib_dir) unless $LOAD_PATH.include?(lib_dir)
$LOAD_PATH.unshift(this_dir) unless $LOAD_PATH.include?(this_dir)

require 'grpc'
require 'optparse'
require 'histogram'
require 'etc'
require 'facter'
require 'qps-common'
require 'src/proto/grpc/testing/services_services_pb'
require 'src/proto/grpc/testing/proxy-service_services_pb'

class ProxyBenchmarkClientServiceImpl < Grpc::Testing::ProxyClientService::Service
  def initialize(port, c_ext, php_client_bin)
    @mytarget = "localhost:" + port.to_s
    @use_c_ext = c_ext
    @php_client_bin = php_client_bin
  end
  def setup(config)
    @config = config
    @histres = config.histogram_params.resolution
    @histmax = config.histogram_params.max_possible
    @histogram = Histogram.new(@histres, @histmax)
    @start_time = Time.now
    @php_pid = Array.new(@config.client_channels)
    (0..@config.client_channels-1).each do |chan|
      Thread.new {
        if @use_c_ext
          puts "Use protobuf c extension"
          command = "php -d extension=" + File.expand_path(File.dirname(__FILE__)) +
            "/../../php/tests/qps/vendor/google/protobuf/php/ext/google/protobuf/modules/protobuf.so " +
            "-d extension=" + File.expand_path(File.dirname(__FILE__)) + "/../../php/ext/grpc/modules/grpc.so " +
            File.expand_path(File.dirname(__FILE__)) + "/" + @php_client_bin + " " + @mytarget + " #{chan%@config.server_targets.length}"
        else
          puts "Use protobuf php extension"
          command = "php -d extension=" + File.expand_path(File.dirname(__FILE__)) + "/../../php/ext/grpc/modules/grpc.so " +
            File.expand_path(File.dirname(__FILE__)) + "/" + @php_client_bin + " " + @mytarget + " #{chan%@config.server_targets.length}"
        end
        puts "[ruby proxy] Starting #{chan}th php-client command use c protobuf #{@use_c_ext}: " + command
        @php_pid[chan] = spawn(command)
        while true
          sleep
        end
      }
    end
  end
  def stop
    (0..@config.client_channels-1).each do |chan|
      Process.kill("TERM", @php_pid[chan])
      Process.wait(@php_pid[chan])
    end
  end
  def get_config(_args, _call)
    @config
  end
  def report_time(call)
    call.each_remote_read do |lat|
      @histogram.add((lat.latency)*1e9)
    end
    Grpc::Testing::Void.new
  end
  def report_hist(call)
    call.each_remote_read do |lat|
      @histogram.merge(lat)
    end
    Grpc::Testing::Void.new
  end
  def mark(reset)
    lat = Grpc::Testing::HistogramData.new(
      bucket: @histogram.contents,
      min_seen: @histogram.minimum,
      max_seen: @histogram.maximum,
      sum: @histogram.sum,
      sum_of_squares: @histogram.sum_of_squares,
      count: @histogram.count
    )
    elapsed = Time.now-@start_time
    if reset
      @start_time = Time.now
      @histogram = Histogram.new(@histres, @histmax)
    end
    Grpc::Testing::ClientStats.new(latencies: lat, time_elapsed: elapsed)
  end
end

class ProxyWorkerServiceImpl < Grpc::Testing::WorkerService::Service
  def cpu_cores
    Facter.value('processors')['count']
  end
  # Leave run_server unimplemented since this proxies for a client only.
  # If the driver tries to use this as a server, it will get an unimplemented
  # status return value.
  def run_client(reqs)
    q = EnumeratorQueue.new(self)
    Thread.new {
      reqs.each do |req|
        case req.argtype.to_s
        when 'setup'
          @bmc.setup(req.setup)
          q.push(Grpc::Testing::ClientStatus.new(stats: @bmc.mark(false)))
        when 'mark'
          q.push(Grpc::Testing::ClientStatus.new(stats:
                                                   @bmc.mark(req.mark.reset)))
        end
      end
      @bmc.stop
      q.push(self)
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
  def initialize(s, bmc)
    @server = s
    @bmc = bmc
  end
end

def proxymain
  options = {
    'driver_port' => 0,
    'php_client_bin' => '../../php/tests/qps/client.php'
  }
  OptionParser.new do |opts|
    opts.banner = 'Usage: [--driver_port <port>]'
    opts.on('--driver_port PORT', '<port>') do |v|
      options['driver_port'] = v
    end
    opts.on("-c", "--[no-]use_protobuf_c_extension", "Use protobuf C-extention") do |c|
      options[:c_ext] = c
    end
    opts.on("-b", "--php_client_bin [FILE]",
      "PHP client to execute; path relative to this script") do |c|
      options['php_client_bin'] = c
    end
  end.parse!

  # Configure any errors with client or server child threads to surface
  Thread.abort_on_exception = true

  # Make sure proxy_server can handle the large number of calls in benchmarks
  s = GRPC::RpcServer.new(pool_size: 1024)
  port = s.add_http2_port("0.0.0.0:" + options['driver_port'].to_s,
                          :this_port_is_insecure)
  bmc = ProxyBenchmarkClientServiceImpl.new(port, options[:c_ext], options['php_client_bin'])
  s.handle(bmc)
  s.handle(ProxyWorkerServiceImpl.new(s, bmc))
  s.run
end

proxymain
