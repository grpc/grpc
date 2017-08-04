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

var os = require('os');
var console = require('console');
var BenchmarkClient = require('./benchmark_client');
var BenchmarkServer = require('./benchmark_server');

module.exports = function WorkerServiceImpl(benchmark_impl, server) {
  var BenchmarkClient;
  var BenchmarkServer;
  switch (benchmark_impl) {
    case 'grpc':
    BenchmarkClient = require('./benchmark_client');
    BenchmarkServer = require('./benchmark_server');
    break;
    case 'express':
    BenchmarkClient = require('./benchmark_client_express');
    BenchmarkServer = require('./benchmark_server_express');
    break;
    default:
    throw new Error('Unrecognized benchmark impl: ' + benchmark_impl);
  }

  this.quitWorker = function quitWorker(call, callback) {
    callback(null, {});
    server.tryShutdown(function() {});
  };

  this.runClient = function runClient(call) {
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
          return;
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
          return;
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

  this.runServer = function runServer(call) {
    var server;
    call.on('data', function(request) {
      var stats;
      switch (request.argtype) {
        case 'setup':
        console.log('ServerConfig %j', request.setup);
        var setup = request.setup;
        var resp_size, generic;
        if (setup.payload_config) {
          switch (setup.payload_config.payload) {
            case 'bytebuf_params':
            resp_size = setup.payload_config.bytebuf_params.resp_size;
            generic = true;
            break;
            case 'simple_params':
            resp_size = setup.payload_config.simple_params.resp_size;
            generic = false;
            break;
            default:
            call.emit('error', new Error('Unsupported PayloadConfig type' +
                setup.payload_config.payload));
            return;
          }
        }
        server = new BenchmarkServer('[::]', request.setup.port,
                                     request.setup.security_params,
                                     generic, resp_size);
        server.on('started', function() {
          stats = server.mark();
          call.write({
            stats: stats,
            port: server.getPort()
          });
        });
        server.start();
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

  this.coreCount = function coreCount(call, callback) {
    callback(null, {cores: os.cpus().length});
  };
};
