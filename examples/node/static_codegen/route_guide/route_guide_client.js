/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

var messages = require('./route_guide_pb');
var services = require('./route_guide_grpc_pb');

var async = require('async');
var fs = require('fs');
var parseArgs = require('minimist');
var path = require('path');
var _ = require('lodash');
var grpc = require('@grpc/grpc-js');

var client = new services.RouteGuideClient('localhost:50051',
                                           grpc.credentials.createInsecure());

var COORD_FACTOR = 1e7;

/**
 * Run the getFeature demo. Calls getFeature with a point known to have a
 * feature and a point known not to have a feature.
 * @param {function} callback Called when this demo is complete
 */
function runGetFeature(callback) {
  var next = _.after(2, callback);
  function featureCallback(error, feature) {
    if (error) {
      callback(error);
      return;
    }
    var latitude = feature.getLocation().getLatitude();
    var longitude = feature.getLocation().getLongitude();
    if (feature.getName() === '') {
      console.log('Found no feature at ' +
          latitude/COORD_FACTOR + ', ' + longitude/COORD_FACTOR);
    } else {
      console.log('Found feature called "' + feature.getName() + '" at ' +
          latitude/COORD_FACTOR + ', ' + longitude/COORD_FACTOR);
    }
    next();
  }
  var point1 = new messages.Point();
  point1.setLatitude(409146138);
  point1.setLongitude(-746188906);
  var point2 = new messages.Point();
  point2.setLatitude(0);
  point2.setLongitude(0);
  client.getFeature(point1, featureCallback);
  client.getFeature(point2, featureCallback);
}

/**
 * Run the listFeatures demo. Calls listFeatures with a rectangle containing all
 * of the features in the pre-generated database. Prints each response as it
 * comes in.
 * @param {function} callback Called when this demo is complete
 */
function runListFeatures(callback) {
  var rect = new messages.Rectangle();
  var lo = new messages.Point();
  lo.setLatitude(400000000);
  lo.setLongitude(-750000000);
  rect.setLo(lo);
  var hi = new messages.Point();
  hi.setLatitude(420000000);
  hi.setLongitude(-730000000);
  rect.setHi(hi);
  console.log('Looking for features between 40, -75 and 42, -73');
  var call = client.listFeatures(rect);
  call.on('data', function(feature) {
      console.log('Found feature called "' + feature.getName() + '" at ' +
          feature.getLocation().getLatitude()/COORD_FACTOR + ', ' +
          feature.getLocation().getLongitude()/COORD_FACTOR);
  });
  call.on('end', callback);
}

/**
 * Run the recordRoute demo. Sends several randomly chosen points from the
 * pre-generated feature database with a variable delay in between. Prints the
 * statistics when they are sent from the server.
 * @param {function} callback Called when this demo is complete
 */
function runRecordRoute(callback) {
  var argv = parseArgs(process.argv, {
    string: 'db_path'
  });
  fs.readFile(path.resolve(argv.db_path), function(err, data) {
    if (err) {
      callback(err);
      return;
    }
    // Transform the loaded features to Feature objects
    var feature_list = _.map(JSON.parse(data), function(value) {
      var feature = new messages.Feature();
      feature.setName(value.name);
      var location = new messages.Point();
      location.setLatitude(value.location.latitude);
      location.setLongitude(value.location.longitude);
      feature.setLocation(location);
      return feature;
    });

    var num_points = 10;
    var call = client.recordRoute(function(error, stats) {
      if (error) {
        callback(error);
        return;
      }
      console.log('Finished trip with', stats.getPointCount(), 'points');
      console.log('Passed', stats.getFeatureCount(), 'features');
      console.log('Travelled', stats.getDistance(), 'meters');
      console.log('It took', stats.getElapsedTime(), 'seconds');
      callback();
    });
    /**
     * Constructs a function that asynchronously sends the given point and then
     * delays sending its callback
     * @param {messages.Point} location The point to send
     * @return {function(function)} The function that sends the point
     */
    function pointSender(location) {
      /**
       * Sends the point, then calls the callback after a delay
       * @param {function} callback Called when complete
       */
      return function(callback) {
        console.log('Visiting point ' + location.getLatitude()/COORD_FACTOR +
            ', ' + location.getLongitude()/COORD_FACTOR);
        call.write(location);
        _.delay(callback, _.random(500, 1500));
      };
    }
    var point_senders = [];
    for (var i = 0; i < num_points; i++) {
      var rand_point = feature_list[_.random(0, feature_list.length - 1)];
      point_senders[i] = pointSender(rand_point.getLocation());
    }
    async.series(point_senders, function() {
      call.end();
    });
  });
}

/**
 * Run the routeChat demo. Send some chat messages, and print any chat messages
 * that are sent from the server.
 * @param {function} callback Called when the demo is complete
 */
function runRouteChat(callback) {
  var call = client.routeChat();
  call.on('data', function(note) {
    console.log('Got message "' + note.getMessage() + '" at ' +
        note.getLocation().getLatitude() + ', ' +
        note.getLocation().getLongitude());
  });

  call.on('end', callback);

  var notes = [{
    location: {
      latitude: 0,
      longitude: 0
    },
    message: 'First message'
  }, {
    location: {
      latitude: 0,
      longitude: 1
    },
    message: 'Second message'
  }, {
    location: {
      latitude: 1,
      longitude: 0
    },
    message: 'Third message'
  }, {
    location: {
      latitude: 0,
      longitude: 0
    },
    message: 'Fourth message'
  }];
  for (var i = 0; i < notes.length; i++) {
    var note = notes[i];
    console.log('Sending message "' + note.message + '" at ' +
        note.location.latitude + ', ' + note.location.longitude);
    var noteMsg = new messages.RouteNote();
    noteMsg.setMessage(note.message);
    var location = new messages.Point();
    location.setLatitude(note.location.latitude);
    location.setLongitude(note.location.longitude);
    noteMsg.setLocation(location);
    call.write(noteMsg);
  }
  call.end();
}

/**
 * Run all of the demos in order
 */
function main() {
  async.series([
    runGetFeature,
    runListFeatures,
    runRecordRoute,
    runRouteChat
  ]);
}

if (require.main === module) {
  main();
}

exports.runGetFeature = runGetFeature;

exports.runListFeatures = runListFeatures;

exports.runRecordRoute = runRecordRoute;

exports.runRouteChat = runRouteChat;
