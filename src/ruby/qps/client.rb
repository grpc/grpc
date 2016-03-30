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
require 'histogram'
require 'src/proto/grpc/testing/services_services'

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
    if config.security_params
      if config.security_params.use_test_ca
        certs = load_test_certs
        cred = GRPC::Core::Credentials.new(certs[0])
      else
        p 'Unsupported to use non-test CA (TBD)'
        exit
      end
      if config.security_params.server_host_override
        p 'Unsupported to use severt host override (TBD)'
        exit
      end
    else
      cred = :this_channel_is_insecure
    end
    @histres = config.histogram_params.resolution
    @histmax = config.histogram_params.max_possible
    @start_time = Time.now
    @histogram = Histogram.new(@histres, @histmax)
    @done = false
    (0..config.client_channels-1).each do |i|
      Thread.new {
        stub = ''
        req = Grpc::Testing::SimpleRequest.new(response_type: Grpc::Testing::PayloadType::COMPRESSABLE,
                                               response_size: config.payload_config.simple_params.resp_size,
                                               payload: Grpc::Testing::Payload.new(type: Grpc::Testing::PayloadType::COMPRESSABLE,
                                                                                   body: nulls(config.payload_config.simple_params.req_size)))
        case config.load_params.load.to_s
        when 'closed_loop'
          waiter = nil
        when 'poisson'
          waiter = Poisson.new(config.load_params.poisson.offered_load / config.client_channels)
        end
        stub = Grpc::Testing::BenchmarkService::Stub.new(config.server_targets[i % config.server_targets.length], cred)
        case config.rpc_type
        when :UNARY
          unary_ping_ponger(req,stub,config,waiter)
        when :STREAMING
          streaming_ping_ponger(req,stub,config,waiter)
        end
      }
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
  end
end
