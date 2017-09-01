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
require 'histogram'
require 'src/proto/grpc/testing/services_services_pb'

class Poisson
  def interarrival
    @lambda_recip * (-Math.log(1.0-rand))
  end
  def advance
    t = @next_time
    @next_time += interarrival
    t
  end
  def initialize(lambda)
    @lambda_recip = 1.0/lambda
    @next_time = Time.now + interarrival
  end
end

class BenchmarkClient
  def initialize(config)
    opts = {}
    if config.security_params
      if config.security_params.use_test_ca
        certs = load_test_certs
        cred = GRPC::Core::ChannelCredentials.new(certs[0])
      else
        cred = GRPC::Core::ChannelCredentials.new()
      end
      if config.security_params.server_host_override
        channel_args = {}
        channel_args[GRPC::Core::Channel::SSL_TARGET] =
          config.security_params.server_host_override
        opts[:channel_args] = channel_args
      end
    else
      cred = :this_channel_is_insecure
    end
    @histres = config.histogram_params.resolution
    @histmax = config.histogram_params.max_possible
    @start_time = Time.now
    @histogram = Histogram.new(@histres, @histmax)
    @done = false

    gtsr = Grpc::Testing::SimpleRequest
    gtpt = Grpc::Testing::PayloadType
    gtp = Grpc::Testing::Payload
    simple_params = config.payload_config.simple_params
    req = gtsr.new(response_type: gtpt::COMPRESSABLE,
                   response_size: simple_params.resp_size,
                   payload: gtp.new(type: gtpt::COMPRESSABLE,
                                    body: nulls(simple_params.req_size)))

    @child_threads = []

    (0..config.client_channels-1).each do |chan|
      gtbss = Grpc::Testing::BenchmarkService::Stub
      st = config.server_targets
      stub = gtbss.new(st[chan % st.length], cred, **opts)
      (0..config.outstanding_rpcs_per_channel-1).each do |r|
        @child_threads << Thread.new {
          case config.load_params.load.to_s
          when 'closed_loop'
            waiter = nil
          when 'poisson'
            waiter = Poisson.new(config.load_params.poisson.offered_load /
                                 (config.client_channels *
                                  config.outstanding_rpcs_per_channel))
          end
          case config.rpc_type
          when :UNARY
            unary_ping_ponger(req,stub,config,waiter)
          when :STREAMING
            streaming_ping_ponger(req,stub,config,waiter)
          end
        }
      end
    end
  end
  def wait_to_issue(waiter)
    if waiter
      delay = waiter.advance-Time.now
      sleep delay if delay > 0
    end
  end
  def unary_ping_ponger(req, stub, config,waiter)
    while !@done
      wait_to_issue(waiter)
      start = Time.now
      resp = stub.unary_call(req)
      @histogram.add((Time.now-start)*1e9)
    end
  end
  def streaming_ping_ponger(req, stub, config, waiter)
    q = EnumeratorQueue.new(self)
    resp = stub.streaming_call(q.each_item)
    start = Time.now
    q.push(req)
    pushed_sentinal = false
    resp.each do |r|
      @histogram.add((Time.now-start)*1e9)
      if !@done
        wait_to_issue(waiter)
        start = Time.now
        q.push(req)
      else
        q.push(self) unless pushed_sentinal
	# Continue polling on the responses to consume and release resources
        pushed_sentinal = true
      end
    end
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
  def shutdown
    @done = true
    @child_threads.each do |thread|
      thread.join
    end
  end
end
