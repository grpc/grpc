/*
 *
 * Copyright 2014, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

var _ = require('underscore');
var ProtoBuf = require('protobufjs');
var fs = require('fs');
var util = require('util');

var Transform = require('stream').Transform;

var builder = ProtoBuf.loadProtoFile(__dirname + '/math.proto');
var math = builder.build('math');

var makeConstructor = require('../surface_server.js').makeServerConstructor;

/**
 * Get a function that deserializes a specific type of protobuf.
 * @param {function()} cls The constructor of the message type to deserialize
 * @return {function(Buffer):cls} The deserialization function
 */
function deserializeCls(cls) {
  /**
   * Deserialize a buffer to a message object
   * @param {Buffer} arg_buf The buffer to deserialize
   * @return {cls} The resulting object
   */
  return function deserialize(arg_buf) {
    return cls.decode(arg_buf);
  };
}

/**
 * Get a function that serializes objects to a buffer by protobuf class.
 * @param {function()} Cls The constructor of the message type to serialize
 * @return {function(Cls):Buffer} The serialization function
 */
function serializeCls(Cls) {
  /**
   * Serialize an object to a Buffer
   * @param {Object} arg The object to serialize
   * @return {Buffer} The serialized object
   */
  return function serialize(arg) {
    return new Buffer(new Cls(arg).encode().toBuffer());
  };
}

/* This function call creates a server constructor for servers that that expose
 * the four specified methods. This specifies how to serialize messages that the
 * server sends and deserialize messages that the client sends, and whether the
 * client or the server will send a stream of messages, for each method. This
 * also specifies a prefix that will be added to method names when sending them
 * on the wire. This function call and all of the preceding code in this file
 * are intended to approximate what the generated code will look like for the
 * math service */
var Server = makeConstructor({
  Div: {
    serialize: serializeCls(math.DivReply),
    deserialize: deserializeCls(math.DivArgs),
    client_stream: false,
    server_stream: false
  },
  Fib: {
    serialize: serializeCls(math.Num),
    deserialize: deserializeCls(math.FibArgs),
    client_stream: false,
    server_stream: true
  },
  Sum: {
    serialize: serializeCls(math.Num),
    deserialize: deserializeCls(math.Num),
    client_stream: true,
    server_stream: false
  },
  DivMany: {
    serialize: serializeCls(math.DivReply),
    deserialize: deserializeCls(math.DivArgs),
    client_stream: true,
    server_stream: true
  }
}, '/Math/');

/**
 * Server function for division. Provides the /Math/DivMany and /Math/Div
 * functions (Div is just DivMany with only one stream element). For each
 * DivArgs parameter, responds with a DivReply with the results of the division
 * @param {Object} call The object containing request and cancellation info
 * @param {function(Error, *)} cb Response callback
 */
function mathDiv(call, cb) {
  var req = call.request;
  if (req.divisor == 0) {
    cb(new Error('cannot divide by zero'));
  }
  cb(null, {
    quotient: req.dividend / req.divisor,
    remainder: req.dividend % req.divisor
  });
}

/**
 * Server function for Fibonacci numbers. Provides the /Math/Fib function. Reads
 * a single parameter that indicates the number of responses, and then responds
 * with a stream of that many Fibonacci numbers.
 * @param {stream} stream The stream for sending responses.
 */
function mathFib(stream) {
  // Here, call is a standard writable Node object Stream
  var previous = 0, current = 1;
  for (var i = 0; i < stream.request.limit; i++) {
    stream.write({num: current});
    var temp = current;
    current += previous;
    previous = temp;
  }
  stream.end();
}

/**
 * Server function for summation. Provides the /Math/Sum function. Reads a
 * stream of number parameters, then responds with their sum.
 * @param {stream} call The stream of arguments.
 * @param {function(Error, *)} cb Response callback
 */
function mathSum(call, cb) {
  // Here, call is a standard readable Node object Stream
  var sum = 0;
  call.on('data', function(data) {
    sum += data.num | 0;
  });
  call.on('end', function() {
    cb(null, {num: sum});
  });
}

function mathDivMany(stream) {
  // Here, call is a standard duplex Node object Stream
  util.inherits(DivTransform, Transform);
  function DivTransform() {
    var options = {objectMode: true};
    Transform.call(this, options);
  }
  DivTransform.prototype._transform = function(div_args, encoding, callback) {
    if (div_args.divisor == 0) {
      callback(new Error('cannot divide by zero'));
    }
    callback(null, {
      quotient: div_args.dividend / div_args.divisor,
      remainder: div_args.dividend % div_args.divisor
    });
  };
  var transform = new DivTransform();
  stream.pipe(transform);
  transform.pipe(stream);
}

var server = new Server({
  Div: mathDiv,
  Fib: mathFib,
  Sum: mathSum,
  DivMany: mathDivMany
});

if (require.main === module) {
  server.bind('localhost:7070').listen();
}

/**
 * See docs for server
 */
module.exports = server;
