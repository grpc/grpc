/*
 *
 * Copyright 2016 gRPC authors.
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

/**
 * Benchmark server module
 * @module
 */

'use strict';

var fs = require('fs');
var path = require('path');
var http = require('http');
var https = require('https');
var EventEmitter = require('events');
var util = require('util');

var express = require('express');
var bodyParser = require('body-parser');

function unaryCall(req, res) {
  var reqObj = req.body;
  var payload = {body: '0'.repeat(reqObj.response_size)};
  res.json(payload);
}

function BenchmarkServer(host, port, tls, generic, response_size) {
  var app = express();
  app.use(bodyParser.json());
  app.put('/serviceProto.BenchmarkService.service/unaryCall', unaryCall);
  this.input_host = host;
  this.input_port = port;
  if (tls) {
    var credentials = {};
    var key_path = path.join(__dirname, '../test/data/server1.key');
    var pem_path = path.join(__dirname, '../test/data/server1.pem');

    var key_data = fs.readFileSync(key_path);
    var pem_data = fs.readFileSync(pem_path);
    credentials['key'] = key_data;
    credentials['cert'] = pem_data;
    this.server = https.createServer(credentials, app);
  } else {
    this.server = http.createServer(app);
  }
}

util.inherits(BenchmarkServer, EventEmitter);

BenchmarkServer.prototype.start = function() {
  var self = this;
  this.server.listen(this.input_port, this.input_hostname, function() {
    self.last_wall_time = process.hrtime();
    self.last_usage = process.cpuUsage();
    self.emit('started');
  });
};

BenchmarkServer.prototype.getPort = function() {
  return this.server.address().port;
};

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

BenchmarkServer.prototype.stop = function(callback) {
  this.server.close(callback);
};

module.exports = BenchmarkServer;
