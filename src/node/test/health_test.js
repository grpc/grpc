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
  var HealthServer = grpc.buildServer([health.service]);
  var healthServer = new HealthServer({
    'grpc.health.v1alpha.Health': new health.Implementation({
      '': 'SERVING',
      'grpc.health.v1alpha.Health': 'SERVING',
      'not.serving.Service': 'NOT_SERVING'
    })
  });
  var healthClient;
  before(function() {
    var port_num = healthServer.bind('0.0.0.0:0');
    healthServer.listen();
    healthClient = new health.Client('localhost:' + port_num);
  });
  after(function() {
    healthServer.shutdown();
  });
  it('should respond with SERVING with no service specified', function(done) {
    healthClient.check({}, function(err, response) {
      assert.ifError(err);
      assert.strictEqual(response.status, 'SERVING');
      done();
    });
  });
  it('should respond that the health check service is SERVING', function(done) {
    healthClient.check({service: 'grpc.health.v1alpha.Health'},
                       function(err, response) {
                         assert.ifError(err);
                         assert.strictEqual(response.status, 'SERVING');
                         done();
                       });
  });
  it('should respond that a disabled service is NOT_SERVING', function(done) {
    healthClient.check({service: 'not.serving.Service'},
                       function(err, response) {
                         assert.ifError(err);
                         assert.strictEqual(response.status, 'NOT_SERVING');
                         done();
                       });
  });
  it('should respond with UNSPECIFIED for an unknown service', function(done) {
    healthClient.check({service: 'unknown.service.Name'},
                       function(err, response) {
                         assert.ifError(err);
                         assert.strictEqual(response.status, 'UNSPECIFIED');
                         done();
                       });
  });
});
