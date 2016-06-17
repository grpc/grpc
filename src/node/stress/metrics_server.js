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

'use strict';

var _ = require('lodash');

var grpc = require('../../..');

var proto = grpc.load(__dirname + '/../../proto/grpc/testing/metrics.proto');
var metrics = proto.grpc.testing;

function getGauge(call, callback) {
  /* jshint validthis: true */
  // Should be bound to a MetricsServer object
  var name = call.request.name;
  if (this.gauges.hasOwnProperty(name)) {
    callback(null, _.assign({name: name}, this.gauges[name]()));
  } else {
    callback({code: grpc.status.NOT_FOUND,
              details: 'No such gauge: ' + name});
  }
}

function getAllGauges(call) {
  /* jshint validthis: true */
  // Should be bound to a MetricsServer object
  _.each(this.gauges, function(getter, name) {
    call.write(_.assign({name: name}, getter()));
  });
  call.end();
}

function MetricsServer(port) {
  var server = new grpc.Server();
  server.addProtoService(metrics.MetricsService.service, {
    getGauge: _.bind(getGauge, this),
    getAllGauges: _.bind(getAllGauges, this)
  });
  server.bind('localhost:' + port, grpc.ServerCredentials.createInsecure());
  this.server = server;
  this.gauges = {};
}

MetricsServer.prototype.start = function() {
  this.server.start();
}

MetricsServer.prototype.registerGauge = function(name, getter) {
  this.gauges[name] = getter;
};

MetricsServer.prototype.shutdown = function() {
  this.server.forceShutdown();
};

module.exports = MetricsServer;
