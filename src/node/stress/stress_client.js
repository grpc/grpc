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

var interop_client = require('../interop/interop_client');
var MetricsServer = require('./metrics_server');

var running;

var metrics_server;

var start_time;
var query_count;

function makeCall(client, test_cases) {
  if (!running) {
    return;
  }
  var test_case = test_cases[_.random(test_cases.length - 1)];
  interop_client.test_cases[test_case].run(client, function() {
    query_count += 1;
    makeCall(client, test_cases);
  });
}

function makeCalls(client, test_cases, parallel_calls_per_channel) {
  _.times(parallel_calls_per_channel, function() {
    makeCall(client, test_cases);
  });
}

function getQps() {
  var diff = process.hrtime(start_time);
  var seconds = diff[0] + diff[1] / 1e9;
  return {long_value: query_count / seconds};
}

function start(server_addresses, test_cases, channels_per_server,
               parallel_calls_per_channel, metrics_port) {
  running = true;
  /* Assuming that we are not calling unimplemented_method. The client class
   * used by empty_unary is (currently) the client class used by every interop
   * test except unimplemented_method */
  var Client = interop_client.test_cases.empty_unary.Client;
  /* Make channels_per_server clients connecting to each server address */
  var channels = _.flatten(_.times(
      channels_per_server, _.partial(_.map, server_addresses, function(address) {
        return new Client(address, grpc.credentials.createInsecure());
      })));
  metrics_server = new MetricsServer(metrics_port);
  metrics_server.registerGauge('qps', getQps);
  start_time = process.hrtime();
  query_count = 0;
  _.each(channels, _.partial(makeCalls, _, test_cases,
                             parallel_calls_per_channel));
  metrics_server.start();
}

function stop() {
  running = false;
  metrics_server.shutdown();
  console.log('QPS: ' + getQps().long_value);
}

function main() {
  var parseArgs = require('minimist');
  var argv = parseArgs(process.argv, {
    string: ['server_addresses', 'test_cases', 'metrics_port'],
    default: {'server_addresses': 'localhost:8080',
              'test_duration_secs': -1,
              'num_channels_per_server': 1,
              'num_stubs_per_channel': 1,
              'metrics_port': '8081'}
  });
  var server_addresses = argv.server_addresses.split(',');
  /* Generate an array of test cases, where the number of instances of each name
   * corresponds to the number given in the argument.
   * e.g. 'empty_unary:1,large_unary:2' =>
   * ['empty_unary', 'large_unary', 'large_unary'] */
  var test_cases = _.flatten(_.map(argv.test_cases.split(','), function(value) {
    var split = value.split(':');
    return _.times(split[1], _.constant(split[0]));
  }));
  start(server_addresses, test_cases, argv.num_channels_per_server,
        argv.num_stubs_per_channel, argv.metrics_port);
  if (argv.test_duration_secs > -1) {
    setTimeout(stop, argv.test_duration_secs * 1000);
  }
}

main();
