/*
 *
 * Copyright 2014, Google Inc.
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

var assert = require('assert');

var surface_server = require('../surface_server.js');

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
