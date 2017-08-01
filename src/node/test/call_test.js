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
var constants = require('../src/constants');

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
    it('should succeed without the new keyword', function() {
      assert.doesNotThrow(function() {
        var call = grpc.Call(channel, 'method', new Date());
        assert(call instanceof grpc.Call);
      });
    });
  });
  describe('deadline', function() {
    it('should time out immediately with negative deadline', function(done) {
      var call = new grpc.Call(channel, 'method', -Infinity);
      var batch = {};
      batch[grpc.opType.RECV_STATUS_ON_CLIENT] = true;
      call.startBatch(batch, function(err, response) {
        assert.strictEqual(response.status.code,
                           constants.status.DEADLINE_EXCEEDED);
        done();
      });
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
  describe('startBatch with message', function() {
    it('should fail with null argument', function() {
      var call = new grpc.Call(channel, 'method', getDeadline(1));
      assert.throws(function() {
        var batch = {};
        batch[grpc.opType.SEND_MESSAGE] = null;
        call.startBatch(batch, function(){});
      }, TypeError);
    });
    it('should fail with numeric argument', function() {
      var call = new grpc.Call(channel, 'method', getDeadline(1));
      assert.throws(function() {
        var batch = {};
        batch[grpc.opType.SEND_MESSAGE] = 5;
        call.startBatch(batch, function(){});
      }, TypeError);
    });
    it('should fail with string argument', function() {
      var call = new grpc.Call(channel, 'method', getDeadline(1));
      assert.throws(function() {
        var batch = {};
        batch[grpc.opType.SEND_MESSAGE] = 'value';
        call.startBatch(batch, function(){});
      }, TypeError);
    });
  });
  describe('startBatch with status', function() {
    it('should fail without a code', function() {
      var call = new grpc.Call(channel, 'method', getDeadline(1));
      assert.throws(function() {
        var batch = {};
        batch[grpc.opType.SEND_STATUS_FROM_SERVER] = {
          details: 'details string',
          metadata: {}
        };
        call.startBatch(batch, function(){});
      }, TypeError);
    });
    it('should fail without details', function() {
      var call = new grpc.Call(channel, 'method', getDeadline(1));
      assert.throws(function() {
        var batch = {};
        batch[grpc.opType.SEND_STATUS_FROM_SERVER] = {
          code: 0,
          metadata: {}
        };
        call.startBatch(batch, function(){});
      }, TypeError);
    });
    it('should fail without metadata', function() {
      var call = new grpc.Call(channel, 'method', getDeadline(1));
      assert.throws(function() {
        var batch = {};
        batch[grpc.opType.SEND_STATUS_FROM_SERVER] = {
          code: 0,
          details: 'details string'
        };
        call.startBatch(batch, function(){});
      }, TypeError);
    });
    it('should fail with incorrectly typed code argument', function() {
      var call = new grpc.Call(channel, 'method', getDeadline(1));
      assert.throws(function() {
        var batch = {};
        batch[grpc.opType.SEND_STATUS_FROM_SERVER] = {
          code: 'code string',
          details: 'details string',
          metadata: {}
        };
        call.startBatch(batch, function(){});
      }, TypeError);
    });
    it('should fail with incorrectly typed details argument', function() {
      var call = new grpc.Call(channel, 'method', getDeadline(1));
      assert.throws(function() {
        var batch = {};
        batch[grpc.opType.SEND_STATUS_FROM_SERVER] = {
          code: 0,
          details: 5,
          metadata: {}
        };
        call.startBatch(batch, function(){});
      }, TypeError);
    });
    it('should fail with incorrectly typed metadata argument', function() {
      var call = new grpc.Call(channel, 'method', getDeadline(1));
      assert.throws(function() {
        var batch = {};
        batch[grpc.opType.SEND_STATUS_FROM_SERVER] = {
          code: 0,
          details: 'details string',
          metadata: 'abc'
        };
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
  describe('cancelWithStatus', function() {
    it('should reject anything other than an integer and a string', function() {
      assert.doesNotThrow(function() {
        var call = new grpc.Call(channel, 'method', getDeadline(1));
        call.cancelWithStatus(1, 'details');
      });
      assert.throws(function() {
        var call = new grpc.Call(channel, 'method', getDeadline(1));
        call.cancelWithStatus();
      });
      assert.throws(function() {
        var call = new grpc.Call(channel, 'method', getDeadline(1));
        call.cancelWithStatus('');
      });
      assert.throws(function() {
        var call = new grpc.Call(channel, 'method', getDeadline(1));
        call.cancelWithStatus(5, {});
      });
    });
    it('should reject the OK status code', function() {
      assert.throws(function() {
        var call = new grpc.Call(channel, 'method', getDeadline(1));
        call.cancelWithStatus(0, 'details');
      });
    });
    it('should result in the call ending with a status', function(done) {
      var call = new grpc.Call(channel, 'method', getDeadline(1));
      var batch = {};
      batch[grpc.opType.RECV_STATUS_ON_CLIENT] = true;
      call.startBatch(batch, function(err, response) {
        assert.strictEqual(response.status.code, 5);
        assert.strictEqual(response.status.details, 'details');
        done();
      });
      call.cancelWithStatus(5, 'details');
    });
  });
  describe('getPeer', function() {
    it('should return a string', function() {
      var call = new grpc.Call(channel, 'method', getDeadline(1));
      assert.strictEqual(typeof call.getPeer(), 'string');
    });
  });
});
