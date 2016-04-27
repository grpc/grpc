// GENERATED CODE -- DO NOT EDIT!

'use strict';
var grpc = require('grpc');
var math_pb = require('./math_pb.js');

function serialize_DivArgs(arg) {
  if (!(arg instanceof math_pb.DivArgs)) {
    throw new Error('Expected argument of type DivArgs');
  }
  return new Buffer(arg.serializeBinary());
}

function deserialize_DivArgs(buffer_arg) {
  return math_pb.DivArgs.deserializeBinary(new Uint8Array(buffer_arg));
}

function serialize_DivReply(arg) {
  if (!(arg instanceof math_pb.DivReply)) {
    throw new Error('Expected argument of type DivReply');
  }
  return new Buffer(arg.serializeBinary());
}

function deserialize_DivReply(buffer_arg) {
  return math_pb.DivReply.deserializeBinary(new Uint8Array(buffer_arg));
}

function serialize_FibArgs(arg) {
  if (!(arg instanceof math_pb.FibArgs)) {
    throw new Error('Expected argument of type FibArgs');
  }
  return new Buffer(arg.serializeBinary());
}

function deserialize_FibArgs(buffer_arg) {
  return math_pb.FibArgs.deserializeBinary(new Uint8Array(buffer_arg));
}

function serialize_Num(arg) {
  if (!(arg instanceof math_pb.Num)) {
    throw new Error('Expected argument of type Num');
  }
  return new Buffer(arg.serializeBinary());
}

function deserialize_Num(buffer_arg) {
  return math_pb.Num.deserializeBinary(new Uint8Array(buffer_arg));
}


var MathService = exports.MathService = {
  div: {
    path: '/math.Math/Div',
    requestStream: false,
    responseStream: false,
    requestType: math_pb.DivArgs,
    responseType: math_pb.DivReply,
    requestSerialize: serialize_DivArgs,
    requestDeserialize: deserialize_DivArgs,
    responseSerialize: serialize_DivReply,
    responseDeserialize: deserialize_DivReply,
  },
  divMany: {
    path: '/math.Math/DivMany',
    requestStream: true,
    responseStream: true,
    requestType: math_pb.DivArgs,
    responseType: math_pb.DivReply,
    requestSerialize: serialize_DivArgs,
    requestDeserialize: deserialize_DivArgs,
    responseSerialize: serialize_DivReply,
    responseDeserialize: deserialize_DivReply,
  },
  fib: {
    path: '/math.Math/Fib',
    requestStream: false,
    responseStream: true,
    requestType: math_pb.FibArgs,
    responseType: math_pb.Num,
    requestSerialize: serialize_FibArgs,
    requestDeserialize: deserialize_FibArgs,
    responseSerialize: serialize_Num,
    responseDeserialize: deserialize_Num,
  },
  sum: {
    path: '/math.Math/Sum',
    requestStream: true,
    responseStream: false,
    requestType: math_pb.Num,
    responseType: math_pb.Num,
    requestSerialize: serialize_Num,
    requestDeserialize: deserialize_Num,
    responseSerialize: serialize_Num,
    responseDeserialize: deserialize_Num,
  },
};

exports.MathClient = grpc.makeGenericClientConstructor(MathService);
