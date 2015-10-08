/*
 *
 * Copyright 2015, Google Inc.
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

'use strict';

var grpc = require('../..');
var math = grpc.load(__dirname + '/math.proto').math;

/**
 * Server function for division. Provides the /Math/DivMany and /Math/Div
 * functions (Div is just DivMany with only one stream element). For each
 * DivArgs parameter, responds with a DivReply with the results of the division
 * @param {Object} call The object containing request and cancellation info
 * @param {function(Error, *)} cb Response callback
 */
function mathDiv(call, cb) {
  var req = call.request;
  // Unary + is explicit coersion to integer
  if (+req.divisor === 0) {
    cb(new Error('cannot divide by zero'));
  } else {
    cb(null, {
      quotient: req.dividend / req.divisor,
      remainder: req.dividend % req.divisor
    });
  }
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
    sum += (+data.num);
  });
  call.on('end', function() {
    cb(null, {num: sum});
  });
}

function mathDivMany(stream) {
  stream.on('data', function(div_args) {
    if (+div_args.divisor === 0) {
      stream.emit('error', new Error('cannot divide by zero'));
    } else {
      stream.write({
        quotient: div_args.dividend / div_args.divisor,
        remainder: div_args.dividend % div_args.divisor
      });
    }
  });
  stream.on('end', function() {
    stream.end();
  });
}

function getMathServer() {
  var server = new grpc.Server();
  server.addProtoService(math.Math.service, {
    div: mathDiv,
    fib: mathFib,
    sum: mathSum,
    divMany: mathDivMany
  });
  return server;
}

if (require.main === module) {
  var server = getMathServer();
  server.bind('0.0.0.0:50051', grpc.ServerCredentials.createInsecure());
  server.start();
}

/**
 * See docs for server
 */
module.exports = getMathServer;
