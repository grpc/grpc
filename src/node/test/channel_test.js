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
var grpc = require('bindings')('grpc.node');

var insecureCreds = grpc.Credentials.createInsecure();

describe('channel', function() {
  describe('constructor', function() {
    it('should require a string for the first argument', function() {
      assert.doesNotThrow(function() {
        new grpc.Channel('hostname', insecureCreds);
      });
      assert.throws(function() {
        new grpc.Channel();
      }, TypeError);
      assert.throws(function() {
        new grpc.Channel(5);
      });
    });
    it('should accept a credential for the second argument', function() {
      assert.doesNotThrow(function() {
        new grpc.Channel('hostname', insecureCreds);
      });
      assert.throws(function() {
        new grpc.Channel('hostname', 5);
      });
    });
    it('should accept an object for the third argument', function() {
      assert.doesNotThrow(function() {
        new grpc.Channel('hostname', insecureCreds, {});
      });
      assert.throws(function() {
        new grpc.Channel('hostname', insecureCreds, 'abc');
      });
    });
    it('should only accept objects with string or int values', function() {
      assert.doesNotThrow(function() {
        new grpc.Channel('hostname', insecureCreds,{'key' : 'value'});
      });
      assert.doesNotThrow(function() {
        new grpc.Channel('hostname', insecureCreds, {'key' : 5});
      });
      assert.throws(function() {
        new grpc.Channel('hostname', insecureCreds, {'key' : null});
      });
      assert.throws(function() {
        new grpc.Channel('hostname', insecureCreds, {'key' : new Date()});
      });
    });
  });
  describe('close', function() {
    it('should succeed silently', function() {
      var channel = new grpc.Channel('hostname', insecureCreds, {});
      assert.doesNotThrow(function() {
        channel.close();
      });
    });
    it('should be idempotent', function() {
      var channel = new grpc.Channel('hostname', insecureCreds, {});
      assert.doesNotThrow(function() {
        channel.close();
        channel.close();
      });
    });
  });
  describe('getTarget', function() {
    it('should return a string', function() {
      var channel = new grpc.Channel('localhost', insecureCreds, {});
      assert.strictEqual(typeof channel.getTarget(), 'string');
    });
  });
});
