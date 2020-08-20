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

# This is the xDS interop test Ruby client. This is meant to be run by
# the run_xds_tests.py test runner.
#
# Usage: $ tools/run_tests/run_xds_tests.py --test_case=... ...
#    --client_cmd="path/to/xds_client.rb --server=<hostname> \
#                                        --stats_port=<port> \
#                                        --qps=<qps>"

# These lines are required for the generated files to load grpc
this_dir = File.expand_path(File.dirname(__FILE__))
lib_dir = File.join(File.dirname(File.dirname(this_dir)), 'lib')
pb_dir = File.dirname(this_dir)
$LOAD_PATH.unshift(lib_dir) unless $LOAD_PATH.include?(lib_dir)
$LOAD_PATH.unshift(pb_dir) unless $LOAD_PATH.include?(pb_dir)

require 'optparse'
require 'logger'

require_relative '../../lib/grpc'
require 'google/protobuf'

require_relative '../src/proto/grpc/testing/empty_pb'
require_relative '../src/proto/grpc/testing/messages_pb'
require_relative '../src/proto/grpc/testing/test_services_pb'

# Some global variables to be shared by server and client
$watchers = Array.new
$watchers_mutex = Mutex.new
$watchers_cv = ConditionVariable.new
$shutdown = false

# RubyLogger defines a logger for gRPC based on the standard ruby logger.
module RubyLogger
  def logger
    LOGGER
  end

  LOGGER = Logger.new(STDOUT)
  LOGGER.level = Logger::INFO
end

# GRPC is the general RPC module
module GRPC
  # Inject the noop #logger if no module-level logger method has been injected.
  extend RubyLogger
end

# creates a test stub
def create_stub(opts)
  address = "#{opts.server}"
  GRPC.logger.info("... connecting insecurely to #{address}")
  Grpc::Testing::TestService::Stub.new(
    address,
    :this_channel_is_insecure,
  )
end

# This implements LoadBalancerStatsService required by the test runner
class TestTarget < Grpc::Testing::LoadBalancerStatsService::Service
  include Grpc::Testing

  def get_client_stats(req, _call)
    finish_time = Process.clock_gettime(Process::CLOCK_MONOTONIC) +
                  req['timeout_sec']
    watcher = {}
    $watchers_mutex.synchronize do
      watcher = {
        "rpcs_by_method" => Hash.new(),
        "rpcs_by_peer" => Hash.new(0),
        "rpcs_needed" => req['num_rpcs'],
        "no_remote_peer" => 0
      }
      $watchers << watcher
      seconds_remaining = finish_time -
                          Process.clock_gettime(Process::CLOCK_MONOTONIC)
      while watcher['rpcs_needed'] > 0 && seconds_remaining > 0
        $watchers_cv.wait($watchers_mutex, seconds_remaining)
        seconds_remaining = finish_time -
                            Process.clock_gettime(Process::CLOCK_MONOTONIC)
      end
      $watchers.delete_at($watchers.index(watcher))
    end
    # convert results into proper proto object
    rpcs_by_method = {}
    watcher['rpcs_by_method'].each do |rpc_name, rpcs_by_peer|
      rpcs_by_method[rpc_name] = LoadBalancerStatsResponse::RpcsByPeer.new(
        rpcs_by_peer: rpcs_by_peer
      )
    end
    LoadBalancerStatsResponse.new(
      rpcs_by_method: rpcs_by_method,
      rpcs_by_peer: watcher['rpcs_by_peer'],
      num_failures: watcher['no_remote_peer'] + watcher['rpcs_needed']
    );
  end
end

# execute 1 RPC and return remote hostname
def execute_rpc(op, fail_on_failed_rpcs)
  remote_peer = ""
  begin
    op.execute
    if op.metadata.key?('hostname')
      remote_peer = op.metadata['hostname']
    end
  rescue GRPC::BadStatus => e
    GRPC.logger.info("ruby xds: rpc failed:|#{e.message}|, " \
                     "this may or may not be expected")
    if fail_on_failed_rpcs
      raise e
    end
  end
  remote_peer
end

# send 1 rpc every 1/qps second
def run_test_loop(stub, target_seconds_between_rpcs, fail_on_failed_rpcs,
                  rpcs_to_send, metadata_to_send)
  include Grpc::Testing
  simple_req = SimpleRequest.new()
  empty_req = Empty.new()
  target_next_start = Process.clock_gettime(Process::CLOCK_MONOTONIC)
  while !$shutdown
    now = Process.clock_gettime(Process::CLOCK_MONOTONIC)
    sleep_seconds = target_next_start - now
    if sleep_seconds < 0
      target_next_start = now + target_seconds_between_rpcs
      GRPC.logger.info(
        "ruby xds: warning, rpc takes too long to finish. " \
        "Deficit = %.1fms. " \
        "If you consistently see this, the qps is too high." \
        % [(sleep_seconds * 1000).abs().round(1)])
    else
      target_next_start += target_seconds_between_rpcs
      sleep(sleep_seconds)
    end
    deadline = GRPC::Core::TimeConsts::from_relative_time(30) # 30 seconds
    results = {}
    rpcs_to_send.each do |rpc|
      metadata = metadata_to_send.key?(rpc) ? metadata_to_send[rpc] : {}
      if rpc == 'UnaryCall'
        op = stub.unary_call(simple_req,
                             metadata: metadata,
                             deadline: deadline,
                             return_op: true)
      elsif rpc == 'EmptyCall'
        op = stub.empty_call(empty_req,
                             metadata: metadata,
                             deadline: deadline,
                             return_op: true)
      else
        raise "Unsupported rpc %s" % [rpc]
      end
      results[rpc] = execute_rpc(op, fail_on_failed_rpcs)
    end
    $watchers_mutex.synchronize do
      $watchers.each do |watcher|
        # this is counted once when each group of all rpcs_to_send were done
        watcher['rpcs_needed'] -= 1
        results.each do |rpc_name, remote_peer|
          if remote_peer.strip.empty?
            # error is counted per individual RPC
            watcher['no_remote_peer'] += 1
          else
            if not watcher['rpcs_by_method'].key?(rpc_name)
              watcher['rpcs_by_method'][rpc_name] = Hash.new(0)
            end
            # increment the remote hostname distribution histogram
            # both by overall, and broken down per RPC
            watcher['rpcs_by_method'][rpc_name][remote_peer] +=  1
            watcher['rpcs_by_peer'][remote_peer] += 1
          end
        end
      end
      $watchers_cv.broadcast
    end
  end
end

# Args is used to hold the command line info.
Args = Struct.new(:fail_on_failed_rpcs, :num_channels,
                  :rpc, :metadata,
                  :server, :stats_port, :qps)

# validates the command line options, returning them as a Hash.
def parse_args
  args = Args.new
  args['fail_on_failed_rpcs'] = false
  args['num_channels'] = 1
  args['rpc'] = 'UnaryCall'
  args['metadata'] = ''
  OptionParser.new do |opts|
    opts.on('--fail_on_failed_rpcs BOOL', ['false', 'true']) do |v|
      args['fail_on_failed_rpcs'] = v == 'true'
    end
    opts.on('--num_channels CHANNELS', 'number of channels') do |v|
      args['num_channels'] = v.to_i
    end
    opts.on('--rpc RPCS_TO_SEND', 'list of RPCs to send') do |v|
      args['rpc'] = v
    end
    opts.on('--metadata METADATA_TO_SEND', 'metadata to send per RPC') do |v|
      args['metadata'] = v
    end
    opts.on('--server SERVER_HOST', 'server hostname') do |v|
      GRPC.logger.info("ruby xds: server address is #{v}")
      args['server'] = v
    end
    opts.on('--stats_port STATS_PORT', 'stats port') do |v|
      GRPC.logger.info("ruby xds: stats port is #{v}")
      args['stats_port'] = v
    end
    opts.on('--qps QPS', 'qps') do |v|
      GRPC.logger.info("ruby xds: qps is #{v}")
      args['qps'] = v
    end
  end.parse!
  args
end

def main
  opts = parse_args

  # This server hosts the LoadBalancerStatsService
  host = "0.0.0.0:#{opts['stats_port']}"
  s = GRPC::RpcServer.new
  s.add_http2_port(host, :this_port_is_insecure)
  s.handle(TestTarget)
  server_thread = Thread.new {
    # run the server until the main test runner terminates this process
    s.run_till_terminated_or_interrupted(['TERM'])
  }

  # The client just sends unary rpcs continuously in a regular interval
  stub = create_stub(opts)
  target_seconds_between_rpcs = (1.0 / opts['qps'].to_f)
  rpcs_to_send = []
  if opts['rpc']
    rpcs_to_send = opts['rpc'].split(',')
  end
  # Convert 'metadata' input in the form of
  #   rpc1:k1:v1,rpc2:k2:v2,rpc1:k3:v3
  # into
  #   {
  #     'rpc1' => {
  #       'k1' => 'v1',
  #       'k3' => 'v3',
  #     },
  #     'rpc2' => {
  #       'k2' => 'v2'
  #     },
  #   }
  metadata_to_send = {}
  if opts['metadata']
    metadata_entries = opts['metadata'].split(',')
    metadata_entries.each do |e|
      (rpc_name, metadata_key, metadata_value) = e.split(':')
      # initialize if we haven't seen this rpc_name yet
      if !metadata_to_send.key?(rpc_name)
        metadata_to_send[rpc_name] = {}
      end
      metadata_to_send[rpc_name][metadata_key] = metadata_value
    end
  end
  client_threads = Array.new
  opts['num_channels'].times {
    client_threads << Thread.new {
      run_test_loop(stub, target_seconds_between_rpcs,
                    opts['fail_on_failed_rpcs'],
                    rpcs_to_send, metadata_to_send)
    }
  }

  server_thread.join
  $shutdown = true
  client_threads.each { |thd| thd.join }
end

if __FILE__ == $0
  main
end
