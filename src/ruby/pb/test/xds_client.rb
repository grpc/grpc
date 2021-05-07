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

class RpcConfig
  attr_reader :rpcs_to_send, :metadata_to_send, :timeout_sec
  def init(rpcs_to_send, metadata_to_send, timeout_sec = 0)
    @rpcs_to_send = rpcs_to_send
    @metadata_to_send = metadata_to_send
    @timeout_sec = timeout_sec
  end
end

# Some global constant mappings
$RPC_MAP = {
  'UnaryCall' => :UNARY_CALL,
  'EmptyCall' => :EMPTY_CALL,
}

# Some global variables to be shared by server and client
$watchers = Array.new
$watchers_mutex = Mutex.new
$watchers_cv = ConditionVariable.new
$shutdown = false
# These can be configured by the test runner dynamically
$rpc_config = RpcConfig.new
$rpc_config.init([:UNARY_CALL], {})
# These stats are shared across threads
$accumulated_stats_mu = Mutex.new
$num_rpcs_started_by_method = {}
$num_rpcs_succeeded_by_method = {}
$num_rpcs_failed_by_method = {}
$accumulated_method_stats = {}

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

class StatsPerMethod
  attr_reader :rpcs_started, :result
  def initialize()
    @rpcs_started = 0
    @result = Hash.new(0)
  end
  def increment_rpcs_started()
    @rpcs_started += 1
  end
  def add_result(status_code)
    @result[status_code] += 1
  end
end

class ConfigureTarget < Grpc::Testing::XdsUpdateClientConfigureService::Service
  include Grpc::Testing

  def configure(req, _call)
    metadata_to_send = {}
    req.metadata.each do |m|
      rpc = m.type
      if !metadata_to_send.key?(rpc)
        metadata_to_send[rpc] = {}
      end
      metadata_key = m.key
      metadata_value = m.value
      metadata_to_send[rpc][metadata_key] = metadata_value
    end
    new_rpc_config = RpcConfig.new
    new_rpc_config.init(req['types'], metadata_to_send, req['timeout_sec'])
    $rpc_config = new_rpc_config
    ClientConfigureResponse.new()
  end
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
    )
  end

  def get_client_accumulated_stats(req, _call)
    $accumulated_stats_mu.synchronize do
      all_stats_per_method = $accumulated_method_stats.map { |rpc, stats_per_method|
        [rpc,
         LoadBalancerAccumulatedStatsResponse::MethodStats.new(
          rpcs_started: stats_per_method.rpcs_started,
          result: stats_per_method.result
         )]
      }.to_h
      LoadBalancerAccumulatedStatsResponse.new(
        num_rpcs_started_by_method: $num_rpcs_started_by_method,
        num_rpcs_succeeded_by_method: $num_rpcs_succeeded_by_method,
        num_rpcs_failed_by_method: $num_rpcs_failed_by_method,
        stats_per_method: all_stats_per_method,
      )
    end
  end
end

# execute 1 RPC and return remote hostname
def execute_rpc(op, fail_on_failed_rpcs, rpc_stats_key)
  remote_peer = ""
  status_code = 0
  begin
    op.execute
    if op.metadata.key?('hostname')
      remote_peer = op.metadata['hostname']
    end
  rescue GRPC::BadStatus => e
    if fail_on_failed_rpcs
      raise e
    end
    status_code = e.code
  end
  $accumulated_stats_mu.synchronize do
    $accumulated_method_stats[rpc_stats_key].add_result(status_code)
    if remote_peer.empty?
      $num_rpcs_failed_by_method[rpc_stats_key] += 1
    else
      $num_rpcs_succeeded_by_method[rpc_stats_key] += 1
    end
  end
  remote_peer
end

def execute_rpc_in_thread(op, rpc_stats_key)
  Thread.new {
    begin
      op.execute
      # The following should _not_ happen with the current spec
      # because we are only executing RPCs in a thread if we expect it
      # to be kept open, or deadline_exceeded, or dropped by the load
      # balancing policy. These RPCs should not complete successfully.
      # Doing this for consistency
      $accumulated_stats_mu.synchronize do
        $num_rpcs_succeeded_by_method[rpc_stats_key] += 1
        $accumulated_method_stats[rpc_stats_key].add_result(0)
      end
    rescue GRPC::BadStatus => e
      # Normal execution arrives here,
      # either because of deadline_exceeded or "call dropped by load
      # balancing policy"
      $accumulated_stats_mu.synchronize do
        $num_rpcs_failed_by_method[rpc_stats_key] += 1
        $accumulated_method_stats[rpc_stats_key].add_result(e.code)
      end
    end
  }
end

# send 1 rpc every 1/qps second
def run_test_loop(stub, target_seconds_between_rpcs, fail_on_failed_rpcs)
  include Grpc::Testing
  simple_req = SimpleRequest.new()
  empty_req = Empty.new()
  target_next_start = Process.clock_gettime(Process::CLOCK_MONOTONIC)
  # Some RPCs are meant to be "kept open". Since Ruby does not have an
  # async API, we are executing those RPCs in a thread so that they don't
  # block.
  keep_open_threads = Array.new
  while !$shutdown
    now = Process.clock_gettime(Process::CLOCK_MONOTONIC)
    sleep_seconds = target_next_start - now
    if sleep_seconds < 0
      target_next_start = now + target_seconds_between_rpcs
    else
      target_next_start += target_seconds_between_rpcs
      sleep(sleep_seconds)
    end
    deadline_sec = $rpc_config.timeout_sec > 0 ? $rpc_config.timeout_sec : 30
    deadline = GRPC::Core::TimeConsts::from_relative_time(deadline_sec)
    results = {}
    $rpc_config.rpcs_to_send.each do |rpc|
      # rpc is in the form of :UNARY_CALL or :EMPTY_CALL here
      metadata = $rpc_config.metadata_to_send.key?(rpc) ?
                   $rpc_config.metadata_to_send[rpc] : {}
      $accumulated_stats_mu.synchronize do
        $num_rpcs_started_by_method[rpc.to_s] += 1
        $accumulated_method_stats[rpc.to_s].increment_rpcs_started()
      end
      if rpc == :UNARY_CALL
        op = stub.unary_call(simple_req,
                             metadata: metadata,
                             deadline: deadline,
                             return_op: true)
      elsif rpc == :EMPTY_CALL
        op = stub.empty_call(empty_req,
                             metadata: metadata,
                             deadline: deadline,
                             return_op: true)
      else
        raise "Unsupported rpc #{rpc}"
      end
      rpc_stats_key = rpc.to_s
      if metadata.key?('rpc-behavior') or metadata.key?('fi_testcase')
        keep_open_threads << execute_rpc_in_thread(op, rpc_stats_key)
      else
        results[rpc] = execute_rpc(op, fail_on_failed_rpcs, rpc_stats_key)
      end
    end
    $watchers_mutex.synchronize do
      $watchers.each do |watcher|
        # this is counted once when each group of all rpcs_to_send were done
        watcher['rpcs_needed'] -= 1
        results.each do |rpc_name, remote_peer|
          # These stats expect rpc_name to be in the form of
          # UnaryCall or EmptyCall, not the underscore-case all-caps form
          rpc_name = $RPC_MAP.invert()[rpc_name]
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
  keep_open_threads.each { |thd| thd.join }
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
  s.handle(ConfigureTarget)
  server_thread = Thread.new {
    # run the server until the main test runner terminates this process
    s.run_till_terminated_or_interrupted(['TERM'])
  }

  # Initialize stats
  $RPC_MAP.values.each do |rpc|
    $num_rpcs_started_by_method[rpc.to_s] = 0
    $num_rpcs_succeeded_by_method[rpc.to_s] = 0
    $num_rpcs_failed_by_method[rpc.to_s] = 0
    $accumulated_method_stats[rpc.to_s] = StatsPerMethod.new
  end

  # The client just sends rpcs continuously in a regular interval
  stub = create_stub(opts)
  target_seconds_between_rpcs = (1.0 / opts['qps'].to_f)
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
  rpcs_to_send = []
  metadata_to_send = {}
  if opts['metadata']
    metadata_entries = opts['metadata'].split(',')
    metadata_entries.each do |e|
      (rpc_name, metadata_key, metadata_value) = e.split(':')
      rpc_name = $RPC_MAP[rpc_name]
      # initialize if we haven't seen this rpc_name yet
      if !metadata_to_send.key?(rpc_name)
        metadata_to_send[rpc_name] = {}
      end
      metadata_to_send[rpc_name][metadata_key] = metadata_value
    end
  end
  if opts['rpc']
    rpcs_to_send = opts['rpc'].split(',')
  end
  if rpcs_to_send.size > 0
    rpcs_to_send.map! { |rpc| $RPC_MAP[rpc] }
    new_rpc_config = RpcConfig.new
    new_rpc_config.init(rpcs_to_send, metadata_to_send)
    $rpc_config = new_rpc_config
  end
  client_threads = Array.new
  opts['num_channels'].times {
    client_threads << Thread.new {
      run_test_loop(stub, target_seconds_between_rpcs,
                    opts['fail_on_failed_rpcs'])
    }
  }

  server_thread.join
  $shutdown = true
  client_threads.each { |thd| thd.join }
end

if __FILE__ == $0
  main
end
