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

var health = require('../health_check/health.js');

var grpc = require('../');

describe('Health Checking', function() {
  var statusMap = {
    '': 'SERVING',
    'grpc.test.TestServiceNotServing': 'NOT_SERVING',
    'grpc.test.TestServiceServing': 'SERVING'
  };
  var healthServer;
  var healthImpl;
  var healthClient;
  before(function() {
    healthServer = new grpc.Server();
    healthImpl = new health.Implementation(statusMap);
    healthServer.addProtoService(health.service, healthImpl);
    var port_num = healthServer.bind('0.0.0.0:0',
                                     grpc.ServerCredentials.createInsecure());
    healthServer.start();
    healthClient = new health.Client('localhost:' + port_num,
                                     grpc.credentials.createInsecure());
  });
  after(function() {
    healthServer.forceShutdown();
  });
  it('should say an enabled service is SERVING', function(done) {
    healthClient.check({service: ''}, function(err, response) {
      assert.ifError(err);
      assert.strictEqual(response.status, 'SERVING');
      done();
    });
  });
  it('should say that a disabled service is NOT_SERVING', function(done) {
    healthClient.check({service: 'grpc.test.TestServiceNotServing'},
                       function(err, response) {
                         assert.ifError(err);
                         assert.strictEqual(response.status, 'NOT_SERVING');
                         done();
                       });
  });
  it('should say that an enabled service is SERVING', function(done) {
    healthClient.check({service: 'grpc.test.TestServiceServing'},
                       function(err, response) {
                         assert.ifError(err);
                         assert.strictEqual(response.status, 'SERVING');
                         done();
                       });
  });
  it('should get NOT_FOUND if the service is not registered', function(done) {
    healthClient.check({service: 'not_registered'}, function(err, response) {
      assert(err);
      assert.strictEqual(err.code, grpc.status.NOT_FOUND);
      done();
    });
  });
  it('should get a different response if the status changes', function(done) {
    healthClient.check({service: 'transient'}, function(err, response) {
      assert(err);
      assert.strictEqual(err.code, grpc.status.NOT_FOUND);
      healthImpl.setStatus('transient', 'SERVING');
      healthClient.check({service: 'transient'}, function(err, response) {
        assert.ifError(err);
        assert.strictEqual(response.status, 'SERVING');
        done();
      });
    });
  });
});
