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

var health = require('../health_check/health');

var health_messages = require('../health_check/v1/health_pb');

var ServingStatus = health_messages.HealthCheckResponse.ServingStatus;

var grpc = require('../');

describe('Health Checking', function() {
  var statusMap = {
    '': ServingStatus.SERVING,
    'grpc.test.TestServiceNotServing': ServingStatus.NOT_SERVING,
    'grpc.test.TestServiceServing': ServingStatus.SERVING
  };
  var healthServer;
  var healthImpl;
  var healthClient;
  before(function() {
    healthServer = new grpc.Server();
    healthImpl = new health.Implementation(statusMap);
    healthServer.addService(health.service, healthImpl);
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
    var request = new health_messages.HealthCheckRequest();
    request.setService('');
    healthClient.check(request, function(err, response) {
      assert.ifError(err);
      assert.strictEqual(response.getStatus(), ServingStatus.SERVING);
      done();
    });
  });
  it('should say that a disabled service is NOT_SERVING', function(done) {
    var request = new health_messages.HealthCheckRequest();
    request.setService('grpc.test.TestServiceNotServing');
    healthClient.check(request, function(err, response) {
      assert.ifError(err);
      assert.strictEqual(response.getStatus(), ServingStatus.NOT_SERVING);
      done();
    });
  });
  it('should say that an enabled service is SERVING', function(done) {
    var request = new health_messages.HealthCheckRequest();
    request.setService('grpc.test.TestServiceServing');
    healthClient.check(request, function(err, response) {
      assert.ifError(err);
      assert.strictEqual(response.getStatus(), ServingStatus.SERVING);
      done();
    });
  });
  it('should get NOT_FOUND if the service is not registered', function(done) {
    var request = new health_messages.HealthCheckRequest();
    request.setService('not_registered');
    healthClient.check(request, function(err, response) {
      assert(err);
      assert.strictEqual(err.code, grpc.status.NOT_FOUND);
      done();
    });
  });
  it('should get a different response if the status changes', function(done) {
    var request = new health_messages.HealthCheckRequest();
    request.setService('transient');
    healthClient.check(request, function(err, response) {
      assert(err);
      assert.strictEqual(err.code, grpc.status.NOT_FOUND);
      healthImpl.setStatus('transient', ServingStatus.SERVING);
      healthClient.check(request, function(err, response) {
        assert.ifError(err);
        assert.strictEqual(response.getStatus(), ServingStatus.SERVING);
        done();
      });
    });
  });
});
