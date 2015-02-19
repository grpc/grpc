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

var surface_client = require('../src/client.js');

var ProtoBuf = require('protobufjs');

var grpc = require('..');

var math_proto = ProtoBuf.loadProtoFile(__dirname + '/../examples/math.proto');

var mathService = math_proto.lookup('math.Math');

describe('Surface server constructor', function() {
  it('Should fail with conflicting method names', function() {
    assert.throws(function() {
      grpc.buildServer([mathService, mathService]);
    });
  });
  it('Should succeed with a single service', function() {
    assert.doesNotThrow(function() {
      grpc.buildServer([mathService]);
    });
  });
  it('Should fail with missing handlers', function() {
    var Server = grpc.buildServer([mathService]);
    assert.throws(function() {
      new Server({
        'math.Math': {
          'div': function() {},
          'divMany': function() {},
          'fib': function() {}
        }
      });
    }, /math.Math.Sum/);
  });
  it('Should fail with no handlers for the service', function() {
    var Server = grpc.buildServer([mathService]);
    assert.throws(function() {
      new Server({});
    }, /math.Math/);
  });
});
describe('Cancelling surface client', function() {
  var client;
  var server;
  before(function() {
    var Server = grpc.buildServer([mathService]);
    server = new Server({
      'math.Math': {
        'div': function(stream) {},
        'divMany': function(stream) {},
        'fib': function(stream) {},
        'sum': function(stream) {}
      }
    });
    var port = server.bind('localhost:0');
    var Client = surface_client.makeClientConstructor(mathService);
    client = new Client('localhost:' + port);
  });
  after(function() {
    server.shutdown();
  });
  it('Should correctly cancel a unary call', function(done) {
    var call = client.div({'divisor': 0, 'dividend': 0}, function(err, resp) {
      assert.strictEqual(err.code, surface_client.status.CANCELLED);
      done();
    });
    call.cancel();
  });
  it('Should correctly cancel a client stream call', function(done) {
    var call = client.sum(function(err, resp) {
      assert.strictEqual(err.code, surface_client.status.CANCELLED);
      done();
    });
    call.cancel();
  });
  it('Should correctly cancel a server stream call', function(done) {
    var call = client.fib({'limit': 5});
    call.on('status', function(status) {
      assert.strictEqual(status.code, surface_client.status.CANCELLED);
      done();
    });
    call.cancel();
  });
  it('Should correctly cancel a bidi stream call', function(done) {
    var call = client.divMany();
    call.on('status', function(status) {
      assert.strictEqual(status.code, surface_client.status.CANCELLED);
      done();
    });
    call.cancel();
  });
});
