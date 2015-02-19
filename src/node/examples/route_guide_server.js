// Copyright 2015, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

var _ = require('underscore');
var grpc = require('..');
var examples = grpc.load(__dirname + '/route_guide.proto').examples;

var Server = grpc.buildServer([examples.RouteGuide.service]);

var COORD_FACTOR = 1e7;

var feature_list = [];

function randomWord(length) {
  var alphabet = 'abcdefghijklmnopqrstuvwxyz';
  var word = '';
  for (var i = 0; i < length; i++) {
    word += alphabet[_.random(0, alphabet.length - 1)];
  }
  return word;
}

function checkFeature(point) {
  var feature;
  for (var i = 0; i < feature_list.length; i++) {
    feature = feature_list[i];
    if (feature.point.latitude === point.latitude &&
        feature.point.longitude === point.longitude) {
      return feature;
    }
  }
  var name;
  if (_.random(0,1) === 0) {
    name = '';
  } else {
    name = randomWord(5);
  }
  feature = {
    name: name,
    location: point
  };
  feature_list.push(feature);
  return feature;
}

function getFeature(call, callback) {
  callback(null, checkFeature(call.request));
}

function listFeatures(call) {
  var lo = call.request.lo;
  var hi = call.request.hi;
  var left = _.min(lo.longitude, hi.longitude);
  var right = _.max(lo.longitude, hi.longitude);
  var top = _.max(lo.latitude, hi.latitude);
  var bottom = _.max(lo.latitude, hi.latitude);
  _.each(feature_list, function(feature) {
    if (feature.location.longitude >= left &&
        feature.location.longitude <= right &&
        feature.location.latitude >= bottom &&
        feature.location.latitude <= top) {
      call.write(feature);
    }
  });
  call.end();
}

/**
 * Calculate the distance between two points using the "haversine" formula.
 * This code was taken from http://www.movable-type.co.uk/scripts/latlong.html.
 * @param start The starting point
 * @param end The end point
 * @return The distance between the points in meters
 */
function getDistance(start, end) {
  var lat1 = start.latitude / COORD_FACTOR;
  var lat2 = end.latitude / COORD_FACTOR;
  var lon1 = start.longitude / COORD_FACTOR;
  var lon2 = end.longitude / COORD_FACTOR;
  var R = 6371000; // metres
  var φ1 = lat1.toRadians();
  var φ2 = lat2.toRadians();
  var Δφ = (lat2-lat1).toRadians();
  var Δλ = (lon2-lon1).toRadians();

  var a = Math.sin(Δφ/2) * Math.sin(Δφ/2) +
      Math.cos(φ1) * Math.cos(φ2) *
      Math.sin(Δλ/2) * Math.sin(Δλ/2);
  var c = 2 * Math.atan2(Math.sqrt(a), Math.sqrt(1-a));

  return R * c;
}

function recordRoute(call, callback) {
  var point_count = 0;
  var feature_count = 0;
  var distance = 0;
  var previous = null;
  var start_time = process.hrtime();
  call.on('data', function(point) {
    point_count += 1;
    if (checkFeature(point) !== '') {
      feature_count += 1;
    }
    if (previous != null) {
      distance += getDistance(previous, point);
    }
    previous = point;
  });
  call.on('end', function() {
    callback(null, {
      point_count: point_count,
      feature_count: feature_count,
      distance: distance|0,
      elapsed_time: process.hrtime(start_time)[0]
    });
  });
}

var route_notes = {};

function pointKey(point) {
  return point.latitude + ' ' + point.longitude;
}

function routeChat(call, callback) {
  call.on('data', function(note) {
    var key = pointKey(note.location);
    if (route_notes.hasOwnProperty(key)) {
      _.each(route_notes[key], function(note) {
        call.write(note);
      });
    } else {
      route_notes[key] = [];
    }
    route_notes[key].push(note);
  });
  call.on('end', function() {
    call.end();
  });
}

function getServer() {
  return new Server({
    'examples.RouteGuide' : {
      getFeature: getFeature,
      listFeatures: listFeatures,
      recordRoute: recordRoute,
      routeChat: routeChat
    }
  });
}

if (require.main === module) {
  var routeServer = getServer();
  routeServer.bind('0.0.0.0:0');
  routeServer.listen();
}

exports.getServer = getServer;
