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
