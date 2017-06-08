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

var assert = require('assert');
var fs = require('fs');
var path = require('path');
var grpc = require('../src/grpc_extension');

describe('server', function() {
  describe('constructor', function() {
    it('should work with no arguments', function() {
      assert.doesNotThrow(function() {
        new grpc.Server();
      });
    });
    it('should work with an empty object argument', function() {
      assert.doesNotThrow(function() {
        new grpc.Server({});
      });
    });
    it('should work without the new keyword', function() {
      var server;
      assert.doesNotThrow(function() {
        server = grpc.Server();
      });
      assert(server instanceof grpc.Server);
    });
    it('should only accept objects with string or int values', function() {
      assert.doesNotThrow(function() {
        new grpc.Server({'key' : 'value'});
      });
      assert.doesNotThrow(function() {
        new grpc.Server({'key' : 5});
      });
      assert.throws(function() {
        new grpc.Server({'key' : null});
      });
      assert.throws(function() {
        new grpc.Server({'key' : new Date()});
      });
    });
  });
  describe('addHttp2Port', function() {
    var server;
    before(function() {
      server = new grpc.Server();
    });
    it('should bind to an unused port', function() {
      var port;
      assert.doesNotThrow(function() {
        port = server.addHttp2Port('0.0.0.0:0',
                                   grpc.ServerCredentials.createInsecure());
      });
      assert(port > 0);
    });
    it('should bind to an unused port with ssl credentials', function() {
      var port;
      var key_path = path.join(__dirname, '../test/data/server1.key');
      var pem_path = path.join(__dirname, '../test/data/server1.pem');
      var key_data = fs.readFileSync(key_path);
      var pem_data = fs.readFileSync(pem_path);
      var creds = grpc.ServerCredentials.createSsl(null,
                                                   [{private_key: key_data,
                                                     cert_chain: pem_data}]);
      assert.doesNotThrow(function() {
        port = server.addHttp2Port('0.0.0.0:0', creds);
      });
      assert(port > 0);
    });
  });
  describe('addSecureHttp2Port', function() {
    var server;
    before(function() {
      server = new grpc.Server();
    });
  });
  describe('start', function() {
    var server;
    before(function() {
      server = new grpc.Server();
      server.addHttp2Port('0.0.0.0:0', grpc.ServerCredentials.createInsecure());
    });
    after(function() {
      server.forceShutdown();
    });
    it('should start without error', function() {
      assert.doesNotThrow(function() {
        server.start();
      });
    });
  });
  describe('shutdown', function() {
    var server;
    beforeEach(function() {
      server = new grpc.Server();
      server.addHttp2Port('0.0.0.0:0', grpc.ServerCredentials.createInsecure());
      server.start();
    });
    afterEach(function() {
      server.forceShutdown();
    });
    it('tryShutdown should shutdown successfully', function(done) {
      server.tryShutdown(done);
    });
    it('forceShutdown should shutdown successfully', function() {
      server.forceShutdown();
    });
    it('tryShutdown should be idempotent', function(done) {
      server.tryShutdown(done);
      server.tryShutdown(function() {});
    });
    it('forceShutdown should be idempotent', function() {
      server.forceShutdown();
      server.forceShutdown();
    });
    it('forceShutdown should trigger tryShutdown', function(done) {
      server.tryShutdown(done);
      server.forceShutdown();
    });
  });
});
