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

# Sample app that connects to an error-throwing implementation of
# Route Guide service.
#
# Usage: $ path/to/route_guide_client.rb

this_dir = File.expand_path(File.dirname(__FILE__))
lib_dir = File.join(File.dirname(this_dir), 'lib')
$LOAD_PATH.unshift(lib_dir) unless $LOAD_PATH.include?(lib_dir)

require 'grpc'
require 'route_guide_services_pb'

include Routeguide

def run_get_feature_expect_error(stub)
  resp = stub.get_feature(Point.new)
end

def run_list_features_expect_error(stub)
  resps = stub.list_features(Rectangle.new)

  # NOOP iteration to pick up error
  resps.each { }
end

def run_record_route_expect_error(stub)
  stub.record_route([])
end

def run_route_chat_expect_error(stub)
  resps = stub.route_chat([])

  # NOOP iteration to pick up error
  resps.each { }
end

def main
  stub = RouteGuide::Stub.new('localhost:50051', :this_channel_is_insecure)

  begin
    run_get_feature_expect_error(stub)
  rescue GRPC::BadStatus => e
    puts "===== GetFeature exception: ====="
    puts e.inspect
    puts "e.code: #{e.code}"
    puts "e.details: #{e.details}"
    puts "e.metadata: #{e.metadata}"
    puts "================================="
  end

  begin
    run_list_features_expect_error(stub)
  rescue GRPC::BadStatus => e
    error = true
    puts "===== ListFeatures exception: ====="
    puts e.inspect
    puts "e.code: #{e.code}"
    puts "e.details: #{e.details}"
    puts "e.metadata: #{e.metadata}"
    puts "================================="
  end

  begin
    run_route_chat_expect_error(stub)
  rescue GRPC::BadStatus => e
    puts "==== RouteChat exception: ===="
    puts e.inspect
    puts "e.code: #{e.code}"
    puts "e.details: #{e.details}"
    puts "e.metadata: #{e.metadata}"
    puts "================================="
  end

  begin
    run_record_route_expect_error(stub)
  rescue GRPC::BadStatus => e
    puts "==== RecordRoute exception: ===="
    puts e.inspect
    puts "e.code: #{e.code}"
    puts "e.details: #{e.details}"
    puts "e.metadata: #{e.metadata}"
    puts "================================="
  end
end

main
