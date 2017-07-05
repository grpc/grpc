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

var console = require('console');
var WorkerServiceImpl = require('./worker_service_impl');

var grpc = require('../../../');
var serviceProto = grpc.load({
  root: __dirname + '/../../..',
  file: 'src/proto/grpc/testing/services.proto'}).grpc.testing;

function runServer(port, benchmark_impl) {
  var server_creds = grpc.ServerCredentials.createInsecure();
  var server = new grpc.Server();
  server.addService(serviceProto.WorkerService.service,
                    new WorkerServiceImpl(benchmark_impl, server));
  var address = '0.0.0.0:' + port;
  server.bind(address, server_creds);
  server.start();
  console.log('running QPS worker on %s', address);
  return server;
}

if (require.main === module) {
  Error.stackTraceLimit = Infinity;
  var parseArgs = require('minimist');
  var argv = parseArgs(process.argv, {
    string: ['driver_port', 'benchmark_impl']
  });
  runServer(argv.driver_port, argv.benchmark_impl);
}

exports.runServer = runServer;
