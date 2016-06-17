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
