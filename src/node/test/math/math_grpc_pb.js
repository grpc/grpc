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
var math_math_pb = require('../math/math_pb.js');

function serialize_DivArgs(arg) {
  if (!(arg instanceof math_math_pb.DivArgs)) {
    throw new Error('Expected argument of type DivArgs');
  }
  return new Buffer(arg.serializeBinary());
}

function deserialize_DivArgs(buffer_arg) {
  return math_math_pb.DivArgs.deserializeBinary(new Uint8Array(buffer_arg));
}

function serialize_DivReply(arg) {
  if (!(arg instanceof math_math_pb.DivReply)) {
    throw new Error('Expected argument of type DivReply');
  }
  return new Buffer(arg.serializeBinary());
}

function deserialize_DivReply(buffer_arg) {
  return math_math_pb.DivReply.deserializeBinary(new Uint8Array(buffer_arg));
}

function serialize_FibArgs(arg) {
  if (!(arg instanceof math_math_pb.FibArgs)) {
    throw new Error('Expected argument of type FibArgs');
  }
  return new Buffer(arg.serializeBinary());
}

function deserialize_FibArgs(buffer_arg) {
  return math_math_pb.FibArgs.deserializeBinary(new Uint8Array(buffer_arg));
}

function serialize_Num(arg) {
  if (!(arg instanceof math_math_pb.Num)) {
    throw new Error('Expected argument of type Num');
  }
  return new Buffer(arg.serializeBinary());
}

function deserialize_Num(buffer_arg) {
  return math_math_pb.Num.deserializeBinary(new Uint8Array(buffer_arg));
}


var MathService = exports.MathService = {
  // Div divides args.dividend by args.divisor and returns the quotient and
  // remainder.
  div: {
    path: '/math.Math/Div',
    requestStream: false,
    responseStream: false,
    requestType: math_math_pb.DivArgs,
    responseType: math_math_pb.DivReply,
    requestSerialize: serialize_DivArgs,
    requestDeserialize: deserialize_DivArgs,
    responseSerialize: serialize_DivReply,
    responseDeserialize: deserialize_DivReply,
  },
  // DivMany accepts an arbitrary number of division args from the client stream
  // and sends back the results in the reply stream.  The stream continues until
  // the client closes its end; the server does the same after sending all the
  // replies.  The stream ends immediately if either end aborts.
  divMany: {
    path: '/math.Math/DivMany',
    requestStream: true,
    responseStream: true,
    requestType: math_math_pb.DivArgs,
    responseType: math_math_pb.DivReply,
    requestSerialize: serialize_DivArgs,
    requestDeserialize: deserialize_DivArgs,
    responseSerialize: serialize_DivReply,
    responseDeserialize: deserialize_DivReply,
  },
  // Fib generates numbers in the Fibonacci sequence.  If args.limit > 0, Fib
  // generates up to limit numbers; otherwise it continues until the call is
  // canceled.  Unlike Fib above, Fib has no final FibReply.
  fib: {
    path: '/math.Math/Fib',
    requestStream: false,
    responseStream: true,
    requestType: math_math_pb.FibArgs,
    responseType: math_math_pb.Num,
    requestSerialize: serialize_FibArgs,
    requestDeserialize: deserialize_FibArgs,
    responseSerialize: serialize_Num,
    responseDeserialize: deserialize_Num,
  },
  // Sum sums a stream of numbers, returning the final result once the stream
  // is closed.
  sum: {
    path: '/math.Math/Sum',
    requestStream: true,
    responseStream: false,
    requestType: math_math_pb.Num,
    responseType: math_math_pb.Num,
    requestSerialize: serialize_Num,
    requestDeserialize: deserialize_Num,
    responseSerialize: serialize_Num,
    responseDeserialize: deserialize_Num,
  },
};

exports.MathClient = grpc.makeGenericClientConstructor(MathService);
