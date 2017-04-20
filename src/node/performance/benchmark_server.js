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

/**
 * Benchmark server module
 * @module
 */

'use strict';

var fs = require('fs');
var path = require('path');
var EventEmitter = require('events');
var util = require('util');

var genericService = require('./generic_service');

var grpc = require('../../../');
var serviceProto = grpc.load({
  root: __dirname + '/../../..',
  file: 'src/proto/grpc/testing/services.proto'}).grpc.testing;

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
 * Handler for the unary benchmark method. Simply responds with a payload
 * containing the requested number of zero bytes.
 * @param {Call} call The call object to be handled
 * @param {function} callback The callback to call with the response
 */
function unaryCall(call, callback) {
  var req = call.request;
  var payload = {body: zeroBuffer(req.response_size)};
  callback(null, {payload: payload});
}

/**
 * Handler for the streaming benchmark method. Simply responds to each request
 * with a payload containing the requested number of zero bytes.
 * @param {Call} call The call object to be handled
 */
function streamingCall(call) {
  call.on('data', function(value) {
    var payload = {body: zeroBuffer(value.response_size)};
    call.write({payload: payload});
  });
  call.on('end', function() {
    call.end();
  });
}

function makeUnaryGenericCall(response_size) {
  var response = zeroBuffer(response_size);
  return function unaryGenericCall(call, callback) {
    callback(null, response);
  };
}

function makeStreamingGenericCall(response_size) {
  var response = zeroBuffer(response_size);
  return function streamingGenericCall(call) {
    call.on('data', function(value) {
      call.write(response);
    });
    call.on('end', function() {
      call.end();
    });
  };
}

/**
 * BenchmarkServer class. Constructed based on parameters from the driver and
 * stores statistics.
 * @param {string} host The host to serve on
 * @param {number} port The port to listen to
 * @param {boolean} tls Indicates whether TLS should be used
 * @param {boolean} generic Indicates whether to use the generic service
 * @param {number=} response_size The response size for the generic service
 */
function BenchmarkServer(host, port, tls, generic, response_size) {
  var server_creds;
  var host_override;
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

  var options = {
    "grpc.max_receive_message_length": -1,
    "grpc.max_send_message_length": -1
  };

  var server = new grpc.Server(options);
  this.port = server.bind(host + ':' + port, server_creds);
  if (generic) {
    server.addService(genericService, {
      unaryCall: makeUnaryGenericCall(response_size),
      streamingCall: makeStreamingGenericCall(response_size)
    });
  } else {
    server.addService(serviceProto.BenchmarkService.service, {
      unaryCall: unaryCall,
      streamingCall: streamingCall
    });
  }
  this.server = server;
}

util.inherits(BenchmarkServer, EventEmitter);

/**
 * Start the benchmark server.
 */
BenchmarkServer.prototype.start = function() {
  this.server.start();
  this.last_wall_time = process.hrtime();
  this.last_usage = process.cpuUsage();
  this.emit('started');
};

/**
 * Return the port number that the server is bound to.
 * @return {Number} The port number
 */
BenchmarkServer.prototype.getPort = function() {
  return this.port;
};

/**
 * Return current statistics for the server. If reset is set, restart
 * statistic collection.
 * @param {boolean} reset Indicates that statistics should be reset
 * @return {object} Server statistics
 */
BenchmarkServer.prototype.mark = function(reset) {
  var wall_time_diff = process.hrtime(this.last_wall_time);
  var usage_diff = process.cpuUsage(this.last_usage);
  if (reset) {
    this.last_wall_time = process.hrtime();
    this.last_usage = process.cpuUsage();
  }
  return {
    time_elapsed: wall_time_diff[0] + wall_time_diff[1] / 1e9,
    time_user: usage_diff.user / 1000000,
    time_system: usage_diff.system / 1000000
  };
};

/**
 * Stop the server.
 * @param {function} callback Called when the server has finished shutting down
 */
BenchmarkServer.prototype.stop = function(callback) {
  this.server.tryShutdown(callback);
};

module.exports = BenchmarkServer;
