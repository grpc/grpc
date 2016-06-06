// GENERATED CODE -- DO NOT EDIT!

// Original file comments:
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
