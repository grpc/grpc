#!/usr/bin/env ruby
# -*- coding: utf-8 -*-

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
# The formula is based on http://mathforum.org/library/drmath/view/51879.html.
def calculate_distance(point_a, point_b)
  to_radians = proc { |x| x * Math::PI / 180 }
  lat_a = to_radians.call(point_a.latitude / COORD_FACTOR)
  lat_b = to_radians.call(point_b.latitude / COORD_FACTOR)
  lon_a = to_radians.call(point_a.longitude / COORD_FACTOR)
  lon_b = to_radians.call(point_b.longitude / COORD_FACTOR)
  delta_lat = lat_a - lat_b
  delta_lon = lon_a - lon_b
  a = Math.sin(delta_lat / 2)**2 +
      Math.cos(lat_a) * Math.cos(lat_b) +
      Math.sin(delta_lon / 2)**2
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
