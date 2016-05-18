// GENERATED CODE -- DO NOT EDIT!

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


var RouteGuideService = exports.RouteGuideService = {
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
