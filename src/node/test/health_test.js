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
