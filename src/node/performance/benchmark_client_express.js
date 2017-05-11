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
var http = require('http');
var https = require('https');

var async = require('async');
var _ = require('lodash');
var PoissonProcess = require('poisson-process');
var Histogram = require('./histogram');

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

function BenchmarkClient(server_targets, channels, histogram_params,
    security_params) {
  var options = {
    method: 'PUT',
    headers: {
      'Content-Type': 'application/json'
    }
  };
  var protocol;
  if (security_params) {
    var ca_path;
    protocol = https;
    this.request = _.bind(https.request, https);
    if (security_params.use_test_ca) {
      ca_path = path.join(__dirname, '../test/data/ca.pem');
      var ca_data = fs.readFileSync(ca_path);
      options.ca = ca_data;
    }
    if (security_params.server_host_override) {
      var host_override = security_params.server_host_override;
      options.servername = host_override;
    }
  } else {
    protocol = http;
  }

  this.request = _.bind(protocol.request, protocol);

  this.client_options = [];

  for (var i = 0; i < channels; i++) {
    var host_port;
    host_port = server_targets[i % server_targets.length].split(':');
    var new_options = _.assign({hostname: host_port[0], port: +host_port[1]}, options);
    this.client_options[i] = new_options;
  }

  this.histogram = new Histogram(histogram_params.resolution,
                                 histogram_params.max_possible);

  this.running = false;

  this.pending_calls = 0;
}

util.inherits(BenchmarkClient, EventEmitter);

function startAllClients(client_options_list, outstanding_rpcs_per_channel,
                         makeCall, emitter) {
  _.each(client_options_list, function(client_options) {
    _.times(outstanding_rpcs_per_channel, function() {
      makeCall(client_options);
    });
  });
}

BenchmarkClient.prototype.startClosedLoop = function(
    outstanding_rpcs_per_channel, rpc_type, req_size, resp_size, generic) {
  var self = this;

  var options = {};

  self.running = true;

  if (rpc_type == 'UNARY') {
    options.path = '/serviceProto.BenchmarkService.service/unaryCall';
  } else {
    self.emit('error', new Error('Unsupported rpc_type: ' + rpc_type));
  }

  if (generic) {
    self.emit('error', new Error('Generic client not supported'));
  }

  self.last_wall_time = process.hrtime();
  self.last_usage = process.cpuUsage();

  var argument = {
    response_size: resp_size,
    payload: {
      body: '0'.repeat(req_size)
    }
  };

  function makeCall(client_options) {
    if (self.running) {
      self.pending_calls++;
      var start_time = process.hrtime();
      function finishCall(success) {
        if (success) {
          var time_diff = process.hrtime(start_time);
          self.histogram.add(timeDiffToNanos(time_diff));
        }
        makeCall(client_options);
        self.pending_calls--;
        if ((!self.running) && self.pending_calls == 0) {
          self.emit('finished');
        }
      }
      var req = self.request(client_options, function(res) {
        var res_data = '';
        res.on('data', function(data) {
          res_data += data;
        });
        res.on('end', function() {
          JSON.parse(res_data);
          finishCall(true);
        });
      });
      req.write(JSON.stringify(argument));
      req.end();
      req.on('error', function(error) {
        if (error.code === 'ECONNRESET' || error.code === 'ETIMEDOUT') {
          finishCall(false);
          return;
        }
        self.emit('error', new Error('Client error: ' + error.message));
        self.running = false;
      });
    }
  }

  startAllClients(_.map(self.client_options, _.partial(_.assign, options)),
                  outstanding_rpcs_per_channel, makeCall, self);
};

BenchmarkClient.prototype.startPoisson = function(
    outstanding_rpcs_per_channel, rpc_type, req_size, resp_size, offered_load,
    generic) {
  var self = this;

  var options = {};

  self.running = true;

  if (rpc_type == 'UNARY') {
    options.path = '/serviceProto.BenchmarkService.service/unaryCall';
  } else {
    self.emit('error', new Error('Unsupported rpc_type: ' + rpc_type));
  }

  if (generic) {
    self.emit('error', new Error('Generic client not supported'));
  }

  self.last_wall_time = process.hrtime();
  self.last_usage = process.cpuUsage();

  var argument = {
    response_size: resp_size,
    payload: {
      body: '0'.repeat(req_size)
    }
  };

  function makeCall(client_options, poisson) {
    if (self.running) {
      self.pending_calls++;
      var start_time = process.hrtime();
      var req = self.request(client_options, function(res) {
        var res_data = '';
        res.on('data', function(data) {
          res_data += data;
        });
        res.on('end', function() {
          JSON.parse(res_data);
          var time_diff = process.hrtime(start_time);
          self.histogram.add(timeDiffToNanos(time_diff));
          self.pending_calls--;
          if ((!self.running) && self.pending_calls == 0) {
            self.emit('finished');
          }
        });
      });
      req.write(JSON.stringify(argument));
      req.end();
      req.on('error', function(error) {
        self.emit('error', new Error('Client error: ' + error.message));
        self.running = false;
      });
    } else {
      poisson.stop();
    }
  }

  var averageIntervalMs = (1 / offered_load) * 1000;

  startAllClients(_.map(self.client_options, _.partial(_.assign, options)),
                  outstanding_rpcs_per_channel, function(opts){
                    var p = PoissonProcess.create(averageIntervalMs, function() {
                      makeCall(opts, p);
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
