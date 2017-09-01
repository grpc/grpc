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

'use strict';

var fs = require('fs');
var path = require('path');
var _ = require('lodash');
var AsyncDelayQueue = require('./async_delay_queue');
var grpc = require('..');
var testProto = grpc.load({
  root: __dirname + '/../../..',
  file: 'src/proto/grpc/testing/test.proto'}).grpc.testing;

var ECHO_INITIAL_KEY = 'x-grpc-test-echo-initial';
var ECHO_TRAILING_KEY = 'x-grpc-test-echo-trailing-bin';

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
 * Echos a header metadata item as specified in the interop spec.
 * @param {Call} call The call to echo metadata on
 */
function echoHeader(call) {
  var echo_initial = call.metadata.get(ECHO_INITIAL_KEY);
  if (echo_initial.length > 0) {
    var response_metadata = new grpc.Metadata();
    response_metadata.set(ECHO_INITIAL_KEY, echo_initial[0]);
    call.sendMetadata(response_metadata);
  }
}

/**
 * Gets the trailer metadata that should be echoed when the call is done,
 * as specified in the interop spec.
 * @param {Call} call The call to get metadata from
 * @return {grpc.Metadata} The metadata to send as a trailer
 */
function getEchoTrailer(call) {
  var echo_trailer = call.metadata.get(ECHO_TRAILING_KEY);
  var response_trailer = new grpc.Metadata();
  if (echo_trailer.length > 0) {
    response_trailer.set(ECHO_TRAILING_KEY, echo_trailer[0]);
  }
  return response_trailer;
}

function getPayload(payload_type, size) {
  var body = zeroBuffer(size);
  return {type: payload_type, body: body};
}

/**
 * Respond to an empty parameter with an empty response.
 * NOTE: this currently does not work due to issue #137
 * @param {Call} call Call to handle
 * @param {function(Error, Object)} callback Callback to call with result
 *     or error
 */
function handleEmpty(call, callback) {
  echoHeader(call);
  callback(null, {}, getEchoTrailer(call));
}

/**
 * Handle a unary request by sending the requested payload
 * @param {Call} call Call to handle
 * @param {function(Error, Object)} callback Callback to call with result or
 *     error
 */
function handleUnary(call, callback) {
  echoHeader(call);
  var req = call.request;
  if (req.response_status) {
    var status = req.response_status;
    status.metadata = getEchoTrailer(call);
    callback(status);
    return;
  }
  var payload = getPayload(req.response_type, req.response_size);
  callback(null, {payload: payload},
           getEchoTrailer(call));
}

/**
 * Respond to a streaming call with the total size of all payloads
 * @param {Call} call Call to handle
 * @param {function(Error, Object)} callback Callback to call with result or
 *     error
 */
function handleStreamingInput(call, callback) {
  echoHeader(call);
  var aggregate_size = 0;
  call.on('data', function(value) {
    aggregate_size += value.payload.body.length;
  });
  call.on('end', function() {
    callback(null, {aggregated_payload_size: aggregate_size},
             getEchoTrailer(call));
  });
}

/**
 * Respond to a payload request with a stream of the requested payloads
 * @param {Call} call Call to handle
 */
function handleStreamingOutput(call) {
  echoHeader(call);
  var delay_queue = new AsyncDelayQueue();
  var req = call.request;
  if (req.response_status) {
    var status = req.response_status;
    status.metadata = getEchoTrailer(call);
    call.emit('error', status);
    return;
  }
  _.each(req.response_parameters, function(resp_param) {
    delay_queue.add(function(next) {
      call.write({payload: getPayload(req.response_type, resp_param.size)});
      next();
    }, resp_param.interval_us);
  });
  delay_queue.add(function(next) {
    call.end(getEchoTrailer(call));
    next();
  });
}

/**
 * Respond to a stream of payload requests with a stream of payload responses as
 * they arrive.
 * @param {Call} call Call to handle
 */
function handleFullDuplex(call) {
  echoHeader(call);
  var delay_queue = new AsyncDelayQueue();
  call.on('data', function(value) {
    if (value.response_status) {
      var status = value.response_status;
      status.metadata = getEchoTrailer(call);
      call.emit('error', status);
      return;
    }
    _.each(value.response_parameters, function(resp_param) {
      delay_queue.add(function(next) {
        call.write({payload: getPayload(value.response_type, resp_param.size)});
        next();
      }, resp_param.interval_us);
    });
  });
  call.on('end', function() {
    delay_queue.add(function(next) {
      call.end(getEchoTrailer(call));
      next();
    });
  });
}

/**
 * Respond to a stream of payload requests with a stream of payload responses
 * after all requests have arrived
 * @param {Call} call Call to handle
 */
function handleHalfDuplex(call) {
  call.emit('error', Error('HalfDuplexCall not yet implemented'));
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
  var server_creds;
  if (tls) {
    var key_path = path.join(__dirname, '../test/data/server1.key');
    var pem_path = path.join(__dirname, '../test/data/server1.pem');

    var key_data = fs.readFileSync(key_path);
    var pem_data = fs.readFileSync(pem_path);
    server_creds = grpc.ServerCredentials.createSsl(null,
                                                    [{private_key: key_data,
                                                      cert_chain: pem_data}]);
  } else {
    server_creds = grpc.ServerCredentials.createInsecure();
  }
  var server = new grpc.Server(options);
  server.addService(testProto.TestService.service, {
    emptyCall: handleEmpty,
    unaryCall: handleUnary,
    streamingOutputCall: handleStreamingOutput,
    streamingInputCall: handleStreamingInput,
    fullDuplexCall: handleFullDuplex,
    halfDuplexCall: handleHalfDuplex
  });
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
  server_obj.server.start();
}

/**
 * See docs for getServer
 */
exports.getServer = getServer;
