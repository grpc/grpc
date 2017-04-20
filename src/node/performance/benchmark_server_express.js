/*
 *
 * Copyright 2016, Google Inc.
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
