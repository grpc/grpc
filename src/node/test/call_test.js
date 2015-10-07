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
var grpc = require('bindings')('grpc_node');

/**
 * Helper function to return an absolute deadline given a relative timeout in
 * seconds.
 * @param {number} timeout_secs The number of seconds to wait before timing out
 * @return {Date} A date timeout_secs in the future
 */
function getDeadline(timeout_secs) {
  var deadline = new Date();
  deadline.setSeconds(deadline.getSeconds() + timeout_secs);
  return deadline;
}

var insecureCreds = grpc.ChannelCredentials.createInsecure();

describe('call', function() {
  var channel;
  var server;
  before(function() {
    server = new grpc.Server();
    var port = server.addHttp2Port('localhost:0',
                                   grpc.ServerCredentials.createInsecure());
    server.start();
    channel = new grpc.Channel('localhost:' + port, insecureCreds);
  });
  after(function() {
    server.forceShutdown();
  });
  describe('constructor', function() {
    it('should reject anything less than 3 arguments', function() {
      assert.throws(function() {
        new grpc.Call();
      }, TypeError);
      assert.throws(function() {
        new grpc.Call(channel);
      }, TypeError);
      assert.throws(function() {
        new grpc.Call(channel, 'method');
      }, TypeError);
    });
    it('should succeed with a Channel, a string, and a date or number',
       function() {
         assert.doesNotThrow(function() {
           new grpc.Call(channel, 'method', new Date());
         });
         assert.doesNotThrow(function() {
           new grpc.Call(channel, 'method', 0);
         });
       });
    it('should accept an optional fourth string parameter', function() {
      assert.doesNotThrow(function() {
        new grpc.Call(channel, 'method', new Date(), 'host_override');
      });
    });
    it('should fail with a closed channel', function() {
      var local_channel = new grpc.Channel('hostname', insecureCreds);
      local_channel.close();
      assert.throws(function() {
        new grpc.Call(channel, 'method');
      });
    });
    it('should fail with other types', function() {
      assert.throws(function() {
        new grpc.Call({}, 'method', 0);
      }, TypeError);
      assert.throws(function() {
        new grpc.Call(channel, null, 0);
      }, TypeError);
      assert.throws(function() {
        new grpc.Call(channel, 'method', 'now');
      }, TypeError);
    });
  });
  describe('startBatch', function() {
    it('should fail without an object and a function', function() {
      var call = new grpc.Call(channel, 'method', getDeadline(1));
      assert.throws(function() {
        call.startBatch();
      });
      assert.throws(function() {
        call.startBatch({});
      });
      assert.throws(function() {
        call.startBatch(null, function(){});
      });
    });
    it('should succeed with an empty object', function(done) {
      var call = new grpc.Call(channel, 'method', getDeadline(1));
      assert.doesNotThrow(function() {
        call.startBatch({}, function(err) {
          assert.ifError(err);
          done();
        });
      });
    });
  });
  describe('startBatch with metadata', function() {
    it('should succeed with a map of strings to string arrays', function(done) {
      var call = new grpc.Call(channel, 'method', getDeadline(1));
      assert.doesNotThrow(function() {
        var batch = {};
        batch[grpc.opType.SEND_INITIAL_METADATA] = {'key1': ['value1'],
                                                    'key2': ['value2']};
        call.startBatch(batch, function(err, resp) {
          assert.ifError(err);
          assert.deepEqual(resp, {'send_metadata': true});
          done();
        });
      });
    });
    it('should succeed with a map of strings to buffer arrays', function(done) {
      var call = new grpc.Call(channel, 'method', getDeadline(1));
      assert.doesNotThrow(function() {
        var batch = {};
        batch[grpc.opType.SEND_INITIAL_METADATA] = {
          'key1-bin': [new Buffer('value1')],
          'key2-bin': [new Buffer('value2')]
        };
        call.startBatch(batch, function(err, resp) {
          assert.ifError(err);
          assert.deepEqual(resp, {'send_metadata': true});
          done();
        });
      });
    });
    it('should fail with other parameter types', function() {
      var call = new grpc.Call(channel, 'method', getDeadline(1));
      assert.throws(function() {
        var batch = {};
        batch[grpc.opType.SEND_INITIAL_METADATA] = undefined;
        call.startBatch(batch, function(){});
      });
      assert.throws(function() {
        var batch = {};
        batch[grpc.opType.SEND_INITIAL_METADATA] = null;
        call.startBatch(batch, function(){});
      }, TypeError);
      assert.throws(function() {
        var batch = {};
        batch[grpc.opType.SEND_INITIAL_METADATA] = 'value';
        call.startBatch(batch, function(){});
      }, TypeError);
      assert.throws(function() {
        var batch = {};
        batch[grpc.opType.SEND_INITIAL_METADATA] = 5;
        call.startBatch(batch, function(){});
      }, TypeError);
    });
  });
  describe('cancel', function() {
    it('should succeed', function() {
      var call = new grpc.Call(channel, 'method', getDeadline(1));
      assert.doesNotThrow(function() {
        call.cancel();
      });
    });
  });
  describe('getPeer', function() {
    it('should return a string', function() {
      var call = new grpc.Call(channel, 'method', getDeadline(1));
      assert.strictEqual(typeof call.getPeer(), 'string');
    });
  });
});
