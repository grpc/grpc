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

require 'optparse'
require 'thread'
require_relative '../pb/test/client'
require_relative './metrics_server'
require_relative '../lib/grpc'

class QpsGauge < Gauge
  @query_count
  @query_mutex
  @start_time

  def initialize
    @query_count = 0
    @query_mutex = Mutex.new
    @start_time = Time.now
  end

  def increment_queries
    @query_mutex.synchronize { @query_count += 1}
  end

  def get_name
    'qps'
  end

  def get_type
    'long'
  end

  def get_value
    (@query_mutex.synchronize { @query_count / (Time.now - @start_time) }).to_i
  end
end

def start_metrics_server(port)
  host = "0.0.0.0:#{port}"
  server = GRPC::RpcServer.new
  server.add_http2_port(host, :this_port_is_insecure)
  service = MetricsServiceImpl.new
  server.handle(service)
  server_thread = Thread.new { server.run_till_terminated }
  [server, service, server_thread]
end

StressArgs = Struct.new(:server_addresses, :test_cases, :duration,
                        :channels_per_server, :concurrent_calls, :metrics_port)

def start(stress_args)
  running = true
  threads = []
  qps_gauge = QpsGauge.new
  metrics_server, metrics_service, metrics_thread =
    start_metrics_server(stress_args.metrics_port)
  metrics_service.register_gauge(qps_gauge)
  stress_args.server_addresses.each do |address|
    stress_args.channels_per_server.times do
      client_args = Args.new
      client_args.host, client_args.port = address.split(':')
      client_args.secure = false
      client_args.test_case = ''
      stub = create_stub(client_args)
      named_tests = NamedTests.new(stub, client_args)
      stress_args.concurrent_calls.times do
        threads << Thread.new do
          while running
            named_tests.method(stress_args.test_cases.sample).call
            qps_gauge.increment_queries
          end
        end
      end
    end
  end
  if stress_args.duration >= 0
    sleep stress_args.duration
    running = false
    metrics_server.stop
    p "QPS: #{qps_gauge.get_value}"
    threads.each { |thd| thd.join; }
  end
  metrics_thread.join
end

def parse_stress_args
  stress_args = StressArgs.new
  stress_args.server_addresses = ['localhost:8080']
  stress_args.test_cases = []
  stress_args.duration = -1
  stress_args.channels_per_server = 1
  stress_args.concurrent_calls = 1
  stress_args.metrics_port = '8081'
  OptionParser.new do |opts|
    opts.on('--server_addresses [LIST]', Array) do |addrs|
      stress_args.server_addresses = addrs
    end
    opts.on('--test_cases cases', Array) do |cases|
      stress_args.test_cases = (cases.map do |item|
                                  split = item.split(':')
                                  [split[0]] * split[1].to_i
                                end).reduce([], :+)
    end
    opts.on('--test_duration_secs [INT]', OptionParser::DecimalInteger) do |time|
      stress_args.duration = time
    end
    opts.on('--num_channels_per_server [INT]', OptionParser::DecimalInteger) do |channels|
      stress_args.channels_per_server = channels
    end
    opts.on('--num_stubs_per_channel [INT]', OptionParser::DecimalInteger) do |stubs|
      stress_args.concurrent_calls = stubs
    end
    opts.on('--metrics_port [port]') do |port|
      stress_args.metrics_port = port
    end
  end.parse!
  stress_args
end

def main
  opts = parse_stress_args
  start(opts)
end

if __FILE__ == $0
  main
end
