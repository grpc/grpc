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
var grpc = require('../src/grpc_extension');

/**
 * This is used for testing functions with multiple asynchronous calls that
 * can happen in different orders. This should be passed the number of async
 * function invocations that can occur last, and each of those should call this
 * function's return value
 * @param {function()} done The function that should be called when a test is
 *     complete.
 * @param {number} count The number of calls to the resulting function if the
 *     test passes.
 * @return {function()} The function that should be called at the end of each
 *     sequence of asynchronous functions.
 */
function multiDone(done, count) {
  return function() {
    count -= 1;
    if (count <= 0) {
      done();
    }
  };
}
var insecureCreds = grpc.ChannelCredentials.createInsecure();

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
    it('should require a credential for the second argument', function() {
      assert.doesNotThrow(function() {
        new grpc.Channel('hostname', insecureCreds);
      });
      assert.throws(function() {
        new grpc.Channel('hostname', 5);
      });
      assert.throws(function() {
        new grpc.Channel('hostname');
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
    it('should succeed without the new keyword', function() {
      assert.doesNotThrow(function() {
        var channel = grpc.Channel('hostname', insecureCreds);
        assert(channel instanceof grpc.Channel);
      });
    });
  });
  describe('close', function() {
    var channel;
    beforeEach(function() {
      channel = new grpc.Channel('hostname', insecureCreds, {});
    });
    it('should succeed silently', function() {
      assert.doesNotThrow(function() {
        channel.close();
      });
    });
    it('should be idempotent', function() {
      assert.doesNotThrow(function() {
        channel.close();
        channel.close();
      });
    });
  });
  describe('getTarget', function() {
    var channel;
    beforeEach(function() {
      channel = new grpc.Channel('hostname', insecureCreds, {});
    });
    it('should return a string', function() {
      assert.strictEqual(typeof channel.getTarget(), 'string');
    });
  });
  describe('getConnectivityState', function() {
    var channel;
    beforeEach(function() {
      channel = new grpc.Channel('hostname', insecureCreds, {});
    });
    it('should return IDLE for a new channel', function() {
      assert.strictEqual(channel.getConnectivityState(),
                         grpc.connectivityState.IDLE);
    });
  });
  describe('watchConnectivityState', function() {
    var channel;
    beforeEach(function() {
      channel = new grpc.Channel('localhost', insecureCreds, {});
    });
    afterEach(function() {
      channel.close();
    });
    it('should time out if called alone', function(done) {
      var old_state = channel.getConnectivityState();
      var deadline = new Date();
      deadline.setSeconds(deadline.getSeconds() + 1);
      channel.watchConnectivityState(old_state, deadline, function(err, value) {
        assert(err);
        done();
      });
    });
    it('should complete if a connection attempt is forced', function(done) {
      var old_state = channel.getConnectivityState();
      var deadline = new Date();
      deadline.setSeconds(deadline.getSeconds() + 1);
      channel.watchConnectivityState(old_state, deadline, function(err, value) {
        assert.ifError(err);
        assert.notEqual(value.new_state, old_state);
        done();
      });
      channel.getConnectivityState(true);
    });
    it('should complete twice if called twice', function(done) {
      done = multiDone(done, 2);
      var old_state = channel.getConnectivityState();
      var deadline = new Date();
      deadline.setSeconds(deadline.getSeconds() + 1);
      channel.watchConnectivityState(old_state, deadline, function(err, value) {
        assert.ifError(err);
        assert.notEqual(value.new_state, old_state);
        done();
      });
      channel.watchConnectivityState(old_state, deadline, function(err, value) {
        assert.ifError(err);
        assert.notEqual(value.new_state, old_state);
        done();
      });
      channel.getConnectivityState(true);
    });
  });
});
