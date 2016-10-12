#!/usr/bin/env ruby
# -*- coding: utf-8 -*-

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

# Sample app that connects to a Route Guide service.
#
# Usage: $ path/to/route_guide_server.rb path/to/route_guide_db.json &

this_dir = File.expand_path(File.dirname(__FILE__))
lib_dir = File.join(File.dirname(this_dir), 'lib')
$LOAD_PATH.unshift(lib_dir) unless $LOAD_PATH.include?(lib_dir)

require 'grpc'
require 'multi_json'
require 'route_guide_services_pb'

include Routeguide
COORD_FACTOR = 1e7
RADIUS = 637_100

# Determines the distance between two points.
def calculate_distance(point_a, point_b)
  to_radians = proc { |x| x * Math::PI / 180 }
  lat_a = point_a.latitude / COORD_FACTOR
  lat_b = point_b.latitude / COORD_FACTOR
  long_a = point_a.longitude / COORD_FACTOR
  long_b = point_b.longitude / COORD_FACTOR
  φ1 = to_radians.call(lat_a)
  φ2 = to_radians.call(lat_b)
  Δφ = to_radians.call(lat_a - lat_b)
  Δλ = to_radians.call(long_a - long_b)
  a = Math.sin(Δφ / 2)**2 +
      Math.cos(φ1) * Math.cos(φ2) +
      Math.sin(Δλ / 2)**2
  (2 * RADIUS *  Math.atan2(Math.sqrt(a), Math.sqrt(1 - a))).to_i
end

# RectangleEnum provides an Enumerator of the points in a feature_db within a
# given Rectangle.
class RectangleEnum
  # @param [Hash] feature_db
  # @param [Rectangle] bounds
  def initialize(feature_db, bounds)
    @feature_db = feature_db
    @bounds = bounds
    lats = [@bounds.lo.latitude, @bounds.hi.latitude]
    longs = [@bounds.lo.longitude, @bounds.hi.longitude]
    @lo_lat, @hi_lat = lats.min, lats.max
    @lo_long, @hi_long = longs.min, longs.max
  end

  # in? determines if location lies within the bounds of this instances
  # Rectangle.
  def in?(location)
    location['longitude'] >= @lo_long &&
      location['longitude'] <= @hi_long &&
      location['latitude'] >= @lo_lat &&
      location['latitude'] <= @hi_lat
  end

  # each yields the features in the instances feature_db that lie within the
  # instance rectangle.
  def each
    return enum_for(:each) unless block_given?
    @feature_db.each_pair do |location, name|
      next unless in?(location)
      next if name.nil? || name == ''
      pt = Point.new(
        Hash[location.each_pair.map { |k, v| [k.to_sym, v] }])
      yield Feature.new(location: pt, name: name)
    end
  end
end

# ServerImpl provides an implementation of the RouteGuide service.
class ServerImpl < RouteGuide::Service
  # @param [Hash] feature_db {location => name}
  def initialize(feature_db)
    @feature_db = feature_db
    @received_notes = Hash.new { |h, k| h[k] = [] }
  end

  def get_feature(point, _call)
    name = @feature_db[{
      'longitude' => point.longitude,
      'latitude' => point.latitude }] || ''
    Feature.new(location: point, name: name)
  end

  def list_features(rectangle, _call)
    RectangleEnum.new(@feature_db, rectangle).each
  end

  def record_route(call)
    started, elapsed_time = 0, 0
    distance, count, features, last = 0, 0, 0, nil
    call.each_remote_read do |point|
      count += 1
      name = @feature_db[{
        'longitude' => point.longitude,
        'latitude' => point.latitude }] || ''
      features += 1 unless name == ''
      if last.nil?
        last = point
        started = Time.now.to_i
        next
      end
      elapsed_time = Time.now.to_i - started
      distance += calculate_distance(point, last)
      last = point
    end
    RouteSummary.new(point_count: count,
                     feature_count: features,
                     distance: distance,
                     elapsed_time: elapsed_time)
  end

  def route_chat(notes)
    RouteChatEnumerator.new(notes, @received_notes).each_item
  end
end

class RouteChatEnumerator
  def initialize(notes, received_notes)
    @notes = notes
    @received_notes = received_notes
  end
  def each_item
    return enum_for(:each_item) unless block_given?
    begin
      @notes.each do |n|
        key = {
          'latitude' => n.location.latitude,
          'longitude' => n.location.longitude
        }
        earlier_msgs = @received_notes[key]
        @received_notes[key] << n.message
        # send back the earlier messages at this point
        earlier_msgs.each do |r|
          yield RouteNote.new(location: n.location, message: r)
        end
      end
    rescue StandardError => e
      fail e # signal completion via an error
    end
  end
end

def main
  if ARGV.length == 0
    fail 'Please specify the path to the route_guide json database'
  end
  raw_data = []
  File.open(ARGV[0]) do |f|
    raw_data = MultiJson.load(f.read)
  end
  feature_db = Hash[raw_data.map { |x| [x['location'], x['name']] }]
  port = '0.0.0.0:50051'
  s = GRPC::RpcServer.new
  s.add_http2_port(port, :this_port_is_insecure)
  GRPC.logger.info("... running insecurely on #{port}")
  s.handle(ServerImpl.new(feature_db))
  s.run_till_terminated
end

main
