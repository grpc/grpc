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
 * Benchmark client module
 * @module
 */

'use strict';

var fs = require('fs');
var path = require('path');
var util = require('util');
var EventEmitter = require('events');

var async = require('async');
var _ = require('lodash');
var PoissonProcess = require('poisson-process');
var Histogram = require('./histogram');

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
 * Convert a time difference, as returned by process.hrtime, to a number of
 * nanoseconds.
 * @param {Array.<number>} time_diff The time diff, represented as
 *     [seconds, nanoseconds]
 * @return {number} The total number of nanoseconds
 */
function timeDiffToNanos(time_diff) {
  return time_diff[0] * 1e9 + time_diff[1];
}

/**
 * The BenchmarkClient class. Opens channels to servers and makes RPCs based on
 * parameters from the driver, and records statistics about those RPCs.
 * @param {Array.<string>} server_targets List of servers to connect to
 * @param {number} channels The total number of channels to open
 * @param {Object} histogram_params Options for setting up the histogram
 * @param {Object=} security_params Options for TLS setup. If absent, don't use
 *     TLS
 */
function BenchmarkClient(server_targets, channels, histogram_params,
    security_params) {
  var options = {
    "grpc.max_receive_message_length": -1,
    "grpc.max_send_message_length": -1
  };
  var creds;
  if (security_params) {
    var ca_path;
    if (security_params.use_test_ca) {
      ca_path = path.join(__dirname, '../test/data/ca.pem');
      var ca_data = fs.readFileSync(ca_path);
      creds = grpc.credentials.createSsl(ca_data);
    } else {
      creds = grpc.credentials.createSsl();
    }
    if (security_params.server_host_override) {
      var host_override = security_params.server_host_override;
      options['grpc.ssl_target_name_override'] = host_override;
      options['grpc.default_authority'] = host_override;
    }
  } else {
    creds = grpc.credentials.createInsecure();
  }

  this.clients = [];
  var GenericClient = grpc.makeGenericClientConstructor(genericService);
  this.genericClients = [];

  for (var i = 0; i < channels; i++) {
    this.clients[i] = new serviceProto.BenchmarkService(
        server_targets[i % server_targets.length], creds, options);
    this.genericClients[i] = new GenericClient(
        server_targets[i % server_targets.length], creds, options);
  }

  this.histogram = new Histogram(histogram_params.resolution,
                                 histogram_params.max_possible);

  this.running = false;

  this.pending_calls = 0;
};

util.inherits(BenchmarkClient, EventEmitter);

/**
 * Start every client in the list of clients by waiting for each to be ready,
 * then starting outstanding_rpcs_per_channel calls on each of them
 * @param {Array<grpc.Client>} client_list The list of clients
 * @param {Number} outstanding_rpcs_per_channel The number of calls to start
 *     on each client
 * @param {function(grpc.Client)} makeCall Function to make a single call on
 *     a single client
 * @param {EventEmitter} emitter The event emitter to send errors on, if
 *     necessary
 */
function startAllClients(client_list, outstanding_rpcs_per_channel, makeCall,
                         emitter) {
  var ready_wait_funcs = _.map(client_list, function(client) {
    return _.partial(grpc.waitForClientReady, client, Infinity);
  });
  async.parallel(ready_wait_funcs, function(err) {
    if (err) {
      emitter.emit('error', err);
      return;
    }

    _.each(client_list, function(client) {
      _.times(outstanding_rpcs_per_channel, function() {
        makeCall(client);
      });
    });
  });
}

/**
 * Start a closed-loop test. For each channel, start
 * outstanding_rpcs_per_channel RPCs. Then, whenever an RPC finishes, start
 * another one.
 * @param {number} outstanding_rpcs_per_channel Number of RPCs to start per
 *     channel
 * @param {string} rpc_type Which method to call. Should be 'UNARY' or
 *     'STREAMING'
 * @param {number} req_size The size of the payload to send with each request
 * @param {number} resp_size The size of payload to request be sent in responses
 * @param {boolean} generic Indicates that the generic (non-proto) clients
 *     should be used
 */
BenchmarkClient.prototype.startClosedLoop = function(
    outstanding_rpcs_per_channel, rpc_type, req_size, resp_size, generic) {
  var self = this;

  self.running = true;

  self.last_wall_time = process.hrtime();

  self.last_usage = process.cpuUsage();

  var makeCall;

  var argument;
  var client_list;
  if (generic) {
    argument = zeroBuffer(req_size);
    client_list = self.genericClients;
  } else {
    argument = {
      response_size: resp_size,
      payload: {
        body: zeroBuffer(req_size)
      }
    };
    client_list = self.clients;
  }

  if (rpc_type == 'UNARY') {
    makeCall = function(client) {
      if (self.running) {
        self.pending_calls++;
        var start_time = process.hrtime();
        client.unaryCall(argument, function(error, response) {
          if (error) {
            self.emit('error', new Error('Client error: ' + error.message));
            self.running = false;
            return;
          }
          var time_diff = process.hrtime(start_time);
          self.histogram.add(timeDiffToNanos(time_diff));
          makeCall(client);
          self.pending_calls--;
          if ((!self.running) && self.pending_calls == 0) {
            self.emit('finished');
          }
        });
      }
    };
  } else {
    makeCall = function(client) {
      if (self.running) {
        self.pending_calls++;
        var start_time = process.hrtime();
        var call = client.streamingCall();
        call.write(argument);
        call.on('data', function() {
        });
        call.on('end', function() {
          var time_diff = process.hrtime(start_time);
          self.histogram.add(timeDiffToNanos(time_diff));
          makeCall(client);
          self.pending_calls--;
          if ((!self.running) && self.pending_calls == 0) {
            self.emit('finished');
          }
        });
        call.on('error', function(error) {
          self.emit('error', new Error('Client error: ' + error.message));
          self.running = false;
        });
      }
    };
  }

  startAllClients(client_list, outstanding_rpcs_per_channel, makeCall, self);
};

/**
 * Start a poisson test. For each channel, this initiates a number of Poisson
 * processes equal to outstanding_rpcs_per_channel, where each Poisson process
 * has the load parameter offered_load.
 * @param {number} outstanding_rpcs_per_channel Number of RPCs to start per
 *     channel
 * @param {string} rpc_type Which method to call. Should be 'UNARY' or
 *     'STREAMING'
 * @param {number} req_size The size of the payload to send with each request
 * @param {number} resp_size The size of payload to request be sent in responses
 * @param {number} offered_load The load parameter for the Poisson process
 * @param {boolean} generic Indicates that the generic (non-proto) clients
 *     should be used
 */
BenchmarkClient.prototype.startPoisson = function(
    outstanding_rpcs_per_channel, rpc_type, req_size, resp_size, offered_load,
    generic) {
  var self = this;

  self.running = true;

  self.last_wall_time = process.hrtime();

  self.last_usage = process.cpuUsage();

  var makeCall;

  var argument;
  var client_list;
  if (generic) {
    argument = zeroBuffer(req_size);
    client_list = self.genericClients;
  } else {
    argument = {
      response_size: resp_size,
      payload: {
        body: zeroBuffer(req_size)
      }
    };
    client_list = self.clients;
  }

  if (rpc_type == 'UNARY') {
    makeCall = function(client, poisson) {
      if (self.running) {
        self.pending_calls++;
        var start_time = process.hrtime();
        client.unaryCall(argument, function(error, response) {
          if (error) {
            self.emit('error', new Error('Client error: ' + error.message));
            self.running = false;
            return;
          }
          var time_diff = process.hrtime(start_time);
          self.histogram.add(timeDiffToNanos(time_diff));
          self.pending_calls--;
          if ((!self.running) && self.pending_calls == 0) {
            self.emit('finished');
          }
        });
      } else {
        poisson.stop();
      }
    };
  } else {
    makeCall = function(client, poisson) {
      if (self.running) {
        self.pending_calls++;
        var start_time = process.hrtime();
        var call = client.streamingCall();
        call.write(argument);
        call.on('data', function() {
        });
        call.on('end', function() {
          var time_diff = process.hrtime(start_time);
          self.histogram.add(timeDiffToNanos(time_diff));
          self.pending_calls--;
          if ((!self.running) && self.pending_calls == 0) {
            self.emit('finished');
          }
        });
        call.on('error', function(error) {
          self.emit('error', new Error('Client error: ' + error.message));
          self.running = false;
        });
      } else {
        poisson.stop();
      }
    };
  }

  var averageIntervalMs = (1 / offered_load) * 1000;

  startAllClients(client_list, outstanding_rpcs_per_channel, function(client){
    var p = PoissonProcess.create(averageIntervalMs, function() {
      makeCall(client, p);
    });
    p.start();
  }, self);
};

/**
 * Return curent statistics for the client. If reset is set, restart
 * statistic collection.
 * @param {boolean} reset Indicates that statistics should be reset
 * @return {object} Client statistics
 */
BenchmarkClient.prototype.mark = function(reset) {
  var wall_time_diff = process.hrtime(this.last_wall_time);
  var usage_diff = process.cpuUsage(this.last_usage);
  var histogram = this.histogram;
  if (reset) {
    this.last_wall_time = process.hrtime();
    this.last_usage = process.cpuUsage();
    this.histogram = new Histogram(histogram.resolution,
                                   histogram.max_possible);
  }

  return {
    latencies: {
      bucket: histogram.getContents(),
      min_seen: histogram.minimum(),
      max_seen: histogram.maximum(),
      sum: histogram.getSum(),
      sum_of_squares: histogram.sumOfSquares(),
      count: histogram.getCount()
    },
    time_elapsed: wall_time_diff[0] + wall_time_diff[1] / 1e9,
    time_user: usage_diff.user / 1000000,
    time_system: usage_diff.system / 1000000
  };
};

/**
 * Stop the clients.
 * @param {function} callback Called when the clients have finished shutting
 *     down
 */
BenchmarkClient.prototype.stop = function(callback) {
  this.running = false;
  this.on('finished', callback);
};

module.exports = BenchmarkClient;
