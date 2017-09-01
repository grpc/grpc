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
  server.addService(metrics.MetricsService.service, {
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
