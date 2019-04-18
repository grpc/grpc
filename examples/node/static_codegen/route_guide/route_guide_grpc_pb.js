// GENERATED CODE -- DO NOT EDIT!

// Original file comments:
// Copyright 2015 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
'use strict';
var grpc = require('grpc');
var route_guide_pb = require('./route_guide_pb.js');

function serialize_Feature(arg) {
  if (!(arg instanceof route_guide_pb.Feature)) {
    throw new Error('Expected argument of type Feature');
  }
  return new Buffer(arg.serializeBinary());
}

function deserialize_Feature(buffer_arg) {
  return route_guide_pb.Feature.deserializeBinary(new Uint8Array(buffer_arg));
}

function serialize_Point(arg) {
  if (!(arg instanceof route_guide_pb.Point)) {
    throw new Error('Expected argument of type Point');
  }
  return new Buffer(arg.serializeBinary());
}

function deserialize_Point(buffer_arg) {
  return route_guide_pb.Point.deserializeBinary(new Uint8Array(buffer_arg));
}

function serialize_Rectangle(arg) {
  if (!(arg instanceof route_guide_pb.Rectangle)) {
    throw new Error('Expected argument of type Rectangle');
  }
  return new Buffer(arg.serializeBinary());
}

function deserialize_Rectangle(buffer_arg) {
  return route_guide_pb.Rectangle.deserializeBinary(new Uint8Array(buffer_arg));
}

function serialize_RouteNote(arg) {
  if (!(arg instanceof route_guide_pb.RouteNote)) {
    throw new Error('Expected argument of type RouteNote');
  }
  return new Buffer(arg.serializeBinary());
}

function deserialize_RouteNote(buffer_arg) {
  return route_guide_pb.RouteNote.deserializeBinary(new Uint8Array(buffer_arg));
}

function serialize_RouteSummary(arg) {
  if (!(arg instanceof route_guide_pb.RouteSummary)) {
    throw new Error('Expected argument of type RouteSummary');
  }
  return new Buffer(arg.serializeBinary());
}

function deserialize_RouteSummary(buffer_arg) {
  return route_guide_pb.RouteSummary.deserializeBinary(new Uint8Array(buffer_arg));
}


// Interface exported by the server.
var RouteGuideService = exports.RouteGuideService = {
  // A simple RPC.
  //
  // Obtains the feature at a given position.
  //
  // A feature with an empty name is returned if there's no feature at the given
  // position.
  getFeature: {
    path: '/routeguide.RouteGuide/GetFeature',
    requestStream: false,
    responseStream: false,
    requestType: route_guide_pb.Point,
    responseType: route_guide_pb.Feature,
    requestSerialize: serialize_Point,
    requestDeserialize: deserialize_Point,
    responseSerialize: serialize_Feature,
    responseDeserialize: deserialize_Feature,
  },
  // A server-to-client streaming RPC.
  //
  // Obtains the Features available within the given Rectangle.  Results are
  // streamed rather than returned at once (e.g. in a response message with a
  // repeated field), as the rectangle may cover a large area and contain a
  // huge number of features.
  listFeatures: {
    path: '/routeguide.RouteGuide/ListFeatures',
    requestStream: false,
    responseStream: true,
    requestType: route_guide_pb.Rectangle,
    responseType: route_guide_pb.Feature,
    requestSerialize: serialize_Rectangle,
    requestDeserialize: deserialize_Rectangle,
    responseSerialize: serialize_Feature,
    responseDeserialize: deserialize_Feature,
  },
  // A client-to-server streaming RPC.
  //
  // Accepts a stream of Points on a route being traversed, returning a
  // RouteSummary when traversal is completed.
  recordRoute: {
    path: '/routeguide.RouteGuide/RecordRoute',
    requestStream: true,
    responseStream: false,
    requestType: route_guide_pb.Point,
    responseType: route_guide_pb.RouteSummary,
    requestSerialize: serialize_Point,
    requestDeserialize: deserialize_Point,
    responseSerialize: serialize_RouteSummary,
    responseDeserialize: deserialize_RouteSummary,
  },
  // A Bidirectional streaming RPC.
  //
  // Accepts a stream of RouteNotes sent while a route is being traversed,
  // while receiving other RouteNotes (e.g. from other users).
  routeChat: {
    path: '/routeguide.RouteGuide/RouteChat',
    requestStream: true,
    responseStream: true,
    requestType: route_guide_pb.RouteNote,
    responseType: route_guide_pb.RouteNote,
    requestSerialize: serialize_RouteNote,
    requestDeserialize: deserialize_RouteNote,
    responseSerialize: serialize_RouteNote,
    responseDeserialize: deserialize_RouteNote,
  },
};

exports.RouteGuideClient = grpc.makeGenericClientConstructor(RouteGuideService);
