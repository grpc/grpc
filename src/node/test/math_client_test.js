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

var grpc = require('..');
var math = require('./math/math_pb');
var MathClient = require('./math/math_grpc_pb').MathClient;

/**
 * Client to use to make requests to a running server.
 */
var math_client;

/**
 * Server to test against
 */
var getServer = require('./math/math_server.js');

var server = getServer();

describe('Math client', function() {
  before(function(done) {
    var port_num = server.bind('0.0.0.0:0',
                               grpc.ServerCredentials.createInsecure());
    server.start();
    math_client = new MathClient('localhost:' + port_num,
                                 grpc.credentials.createInsecure());
    done();
  });
  after(function() {
    server.forceShutdown();
  });
  it('should handle a single request', function(done) {
    var arg = new math.DivArgs();
    arg.setDividend(7);
    arg.setDivisor(4);
    math_client.div(arg, function handleDivResult(err, value) {
      assert.ifError(err);
      assert.equal(value.getQuotient(), 1);
      assert.equal(value.getRemainder(), 3);
      done();
    });
  });
  it('should handle an error from a unary request', function(done) {
    var arg = new math.DivArgs();
    arg.setDividend(7);
    arg.setDivisor(0);
    math_client.div(arg, function handleDivResult(err, value) {
      assert(err);
      done();
    });
  });
  it('should handle a server streaming request', function(done) {
    var arg = new math.FibArgs();
    arg.setLimit(7);
    var call = math_client.fib(arg);
    var expected_results = [1, 1, 2, 3, 5, 8, 13];
    var next_expected = 0;
    call.on('data', function checkResponse(value) {
      assert.equal(value.getNum(), expected_results[next_expected]);
      next_expected += 1;
    });
    call.on('status', function checkStatus(status) {
      assert.strictEqual(status.code, grpc.status.OK);
      done();
    });
  });
  it('should handle a client streaming request', function(done) {
    var call = math_client.sum(function handleSumResult(err, value) {
      assert.ifError(err);
      assert.equal(value.getNum(), 21);
    });
    for (var i = 0; i < 7; i++) {
      var arg = new math.Num();
      arg.setNum(i);
      call.write(arg);
    }
    call.end();
    call.on('status', function checkStatus(status) {
      assert.strictEqual(status.code, grpc.status.OK);
      done();
    });
  });
  it('should handle a bidirectional streaming request', function(done) {
    function checkResponse(index, value) {
      assert.equal(value.getQuotient(), index);
      assert.equal(value.getRemainder(), 1);
    }
    var call = math_client.divMany();
    var response_index = 0;
    call.on('data', function(value) {
      checkResponse(response_index, value);
      response_index += 1;
    });
    for (var i = 0; i < 7; i++) {
      var arg = new math.DivArgs();
      arg.setDividend(2 * i + 1);
      arg.setDivisor(2);
      call.write(arg);
    }
    call.end();
    call.on('status', function checkStatus(status) {
      assert.strictEqual(status.code, grpc.status.OK);
      done();
    });
  });
  it('should handle an error from a bidi request', function(done) {
    var call = math_client.divMany();
    call.on('data', function(value) {
      assert.fail(value, undefined, 'Unexpected data response on failing call',
                  '!=');
    });
    var arg = new math.DivArgs();
    arg.setDividend(7);
    arg.setDivisor(0);
    call.write(arg);
    call.end();
    call.on('error', function checkStatus(status) {
      done();
    });
  });
});
