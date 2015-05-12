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

var fs = require('fs');
var path = require('path');
var _ = require('underscore');
var grpc = require('..');
var testProto = grpc.load(__dirname + '/test.proto').grpc.testing;
var Server = grpc.buildServer([testProto.TestService.service]);

/**
 * Create a buffer filled with size zeroes
 * @param {number} size The length of the buffer
 * @return {Buffer} The new buffer
 */
function zeroBuffer(size) {
  var zeros = new Buffer(size);
  zeros.fill(0);
  return zeros;
}

/**
 * Respond to an empty parameter with an empty response.
 * NOTE: this currently does not work due to issue #137
 * @param {Call} call Call to handle
 * @param {function(Error, Object)} callback Callback to call with result
 *     or error
 */
function handleEmpty(call, callback) {
  callback(null, {});
}

/**
 * Handle a unary request by sending the requested payload
 * @param {Call} call Call to handle
 * @param {function(Error, Object)} callback Callback to call with result or
 *     error
 */
function handleUnary(call, callback) {
  var req = call.request;
  var zeros = zeroBuffer(req.response_size);
  var payload_type = req.response_type;
  if (payload_type === 'RANDOM') {
    payload_type = ['COMPRESSABLE',
                    'UNCOMPRESSABLE'][Math.random() < 0.5 ? 0 : 1];
  }
  callback(null, {payload: {type: payload_type, body: zeros}});
}

/**
 * Respond to a streaming call with the total size of all payloads
 * @param {Call} call Call to handle
 * @param {function(Error, Object)} callback Callback to call with result or
 *     error
 */
function handleStreamingInput(call, callback) {
  var aggregate_size = 0;
  call.on('data', function(value) {
    aggregate_size += value.payload.body.length;
  });
  call.on('end', function() {
    callback(null, {aggregated_payload_size: aggregate_size});
  });
}

/**
 * Respond to a payload request with a stream of the requested payloads
 * @param {Call} call Call to handle
 */
function handleStreamingOutput(call) {
  var req = call.request;
  var payload_type = req.response_type;
  if (payload_type === 'RANDOM') {
    payload_type = ['COMPRESSABLE',
                    'UNCOMPRESSABLE'][Math.random() < 0.5 ? 0 : 1];
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

/**
 * Respond to a stream of payload requests with a stream of payload responses as
 * they arrive.
 * @param {Call} call Call to handle
 */
function handleFullDuplex(call) {
  call.on('data', function(value) {
    var payload_type = value.response_type;
    if (payload_type === 'RANDOM') {
      payload_type = ['COMPRESSABLE',
                      'UNCOMPRESSABLE'][Math.random() < 0.5 ? 0 : 1];
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

/**
 * Respond to a stream of payload requests with a stream of payload responses
 * after all requests have arrived
 * @param {Call} call Call to handle
 */
function handleHalfDuplex(call) {
  throw new Error('HalfDuplexCall not yet implemented');
}

/**
 * Get a server object bound to the given port
 * @param {string} port Port to which to bind
 * @param {boolean} tls Indicates that the bound port should use TLS
 * @return {{server: Server, port: number}} Server object bound to the support,
 *     and port number that the server is bound to
 */
function getServer(port, tls) {
  // TODO(mlumish): enable TLS functionality
  var options = {};
  var server_creds = null;
  if (tls) {
    var key_path = path.join(__dirname, '../test/data/server1.key');
    var pem_path = path.join(__dirname, '../test/data/server1.pem');

    var key_data = fs.readFileSync(key_path);
    var pem_data = fs.readFileSync(pem_path);
    server_creds = grpc.ServerCredentials.createSsl(null,
                                                    key_data,
                                                    pem_data);
  }
  var server = new Server({
    'grpc.testing.TestService' : {
      emptyCall: handleEmpty,
      unaryCall: handleUnary,
      streamingOutputCall: handleStreamingOutput,
      streamingInputCall: handleStreamingInput,
      fullDuplexCall: handleFullDuplex,
      halfDuplexCall: handleHalfDuplex
    }
  }, null, options);
  var port_num = server.bind('0.0.0.0:' + port, server_creds);
  return {server: server, port: port_num};
}

if (require.main === module) {
  var parseArgs = require('minimist');
  var argv = parseArgs(process.argv, {
    string: ['port', 'use_tls']
  });
  var server_obj = getServer(argv.port, argv.use_tls === 'true');
  console.log('Server attaching to port ' + argv.port);
  server_obj.server.listen();
}

/**
 * See docs for getServer
 */
exports.getServer = getServer;
