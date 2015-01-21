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
var grpc = require('..');
var testProto = grpc.load(__dirname + '/test.proto').grpc.testing;
var Server = grpc.buildServer([testProto.TestService.service]);

function zeroBuffer(size) {
  var zeros = new Buffer(size);
  zeros.fill(0);
  return zeros;
}

function handleEmpty(call, callback) {
  callback(null, {});
}

function handleUnary(call, callback) {
  var req = call.request;
  var zeros = zeroBuffer(req.response_size);
  var payload_type = req.response_type;
  if (payload_type === testProto.PayloadType.RANDOM) {
    payload_type = [
      testProto.PayloadType.COMPRESSABLE,
      testProto.PayloadType.UNCOMPRESSABLE][Math.random() < 0.5 ? 0 : 1];
  }
  callback(null, {payload: {type: payload_type, body: zeros}});
}

function handleStreamingInput(call, callback) {
  var aggregate_size = 0;
  call.on('data', function(value) {
    aggregate_size += value.payload.body.limit - value.payload.body.offset;
  });
  call.on('end', function() {
    callback(null, {aggregated_payload_size: aggregate_size});
  });
}

function handleStreamingOutput(call) {
  var req = call.request;
  var payload_type = req.response_type;
  if (payload_type === testProto.PayloadType.RANDOM) {
    payload_type = [
      testProto.PayloadType.COMPRESSABLE,
      testProto.PayloadType.UNCOMPRESSABLE][Math.random() < 0.5 ? 0 : 1];
  }
  _.each(req.response_parameters, function(resp_param) {
    call.write({
      payload: {
        body: zeroBuffer(resp_param.size),
        type: payload_type
      }
    });
  });
  call.end();
}

function handleFullDuplex(call) {
  call.on('data', function(value) {
    var payload_type = value.response_type;
    if (payload_type === testProto.PayloadType.RANDOM) {
      payload_type = [
        testProto.PayloadType.COMPRESSABLE,
        testProto.PayloadType.UNCOMPRESSABLE][Math.random() < 0.5 ? 0 : 1];
    }
    _.each(value.response_parameters, function(resp_param) {
      call.write({
        payload: {
          body: zeroBuffer(resp_param.size),
          type: payload_type
        }
      });
    });
  });
  call.on('end', function() {
    call.end();
  });
}

function handleHalfDuplex(call) {
  throw new Error('HalfDuplexCall not yet implemented');
}

function getServer(port, tls) {
  // TODO(mlumish): enable TLS functionality
  var server = new Server({
    'grpc.testing.TestService' : {
      emptyCall: handleEmpty,
      unaryCall: handleUnary,
      streamingOutputCall: handleStreamingOutput,
      streamingInputCall: handleStreamingInput,
      fullDuplexCall: handleFullDuplex,
      halfDuplexCall: handleHalfDuplex
    }
  });
  server.bind('0.0.0.0:' + port);
  return server;
}

if (require.main === module) {
  var parseArgs = require('minimist');
  var argv = parseArgs(process.argv, {
    string: ['port', 'use_tls']
  });
  var server = getServer(argv.port, argv.use_tls === 'true');
  server.start();
}

/**
 * See docs for getServer
 */
exports.getServer = getServer;
