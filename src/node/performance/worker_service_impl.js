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

var os = require('os');
var console = require('console');
var BenchmarkClient = require('./benchmark_client');
var BenchmarkServer = require('./benchmark_server');

exports.quitWorker = function quitWorker(call, callback) {
  callback(null, {});
  process.exit(0);
}

exports.runClient = function runClient(call) {
  var client;
  call.on('data', function(request) {
    var stats;
    switch (request.argtype) {
      case 'setup':
      var setup = request.setup;
      console.log('ClientConfig %j', setup);
      client = new BenchmarkClient(setup.server_targets,
                                   setup.client_channels,
                                   setup.histogram_params,
                                   setup.security_params);
      client.on('error', function(error) {
        call.emit('error', error);
      });
      var req_size, resp_size, generic;
      switch (setup.payload_config.payload) {
        case 'bytebuf_params':
        req_size = setup.payload_config.bytebuf_params.req_size;
        resp_size = setup.payload_config.bytebuf_params.resp_size;
        generic = true;
        break;
        case 'simple_params':
        req_size = setup.payload_config.simple_params.req_size;
        resp_size = setup.payload_config.simple_params.resp_size;
        generic = false;
        break;
        default:
        call.emit('error', new Error('Unsupported PayloadConfig type' +
            setup.payload_config.payload));
      }
      switch (setup.load_params.load) {
        case 'closed_loop':
        client.startClosedLoop(setup.outstanding_rpcs_per_channel,
                               setup.rpc_type, req_size, resp_size, generic);
        break;
        case 'poisson':
        client.startPoisson(setup.outstanding_rpcs_per_channel,
                            setup.rpc_type, req_size, resp_size,
                            setup.load_params.poisson.offered_load, generic);
        break;
        default:
        call.emit('error', new Error('Unsupported LoadParams type' +
            setup.load_params.load));
      }
      stats = client.mark();
      call.write({
        stats: stats
      });
      break;
      case 'mark':
      if (client) {
        stats = client.mark(request.mark.reset);
        call.write({
          stats: stats
        });
      } else {
        call.emit('error', new Error('Got Mark before ClientConfig'));
      }
      break;
      default:
      throw new Error('Nonexistent client argtype option: ' + request.argtype);
    }
  });
  call.on('end', function() {
    client.stop(function() {
      call.end();
    });
  });
};

exports.runServer = function runServer(call) {
  var server;
  call.on('data', function(request) {
    var stats;
    switch (request.argtype) {
      case 'setup':
      console.log('ServerConfig %j', request.setup);
      server = new BenchmarkServer('[::]', request.setup.port,
                                   request.setup.security_params);
      server.start();
      stats = server.mark();
      call.write({
        stats: stats,
        port: server.getPort()
      });
      break;
      case 'mark':
      if (server) {
        stats = server.mark(request.mark.reset);
        call.write({
          stats: stats,
          port: server.getPort(),
          cores: 1
        });
      } else {
        call.emit('error', new Error('Got Mark before ServerConfig'));
      }
      break;
      default:
      throw new Error('Nonexistent server argtype option');
    }
  });
  call.on('end', function() {
    server.stop(function() {
      call.end();
    });
  });
};

exports.coreCount = function coreCount(call, callback) {
  callback(null, {cores: os.cpus().length});
};
