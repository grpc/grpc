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
var grpc = require('bindings')('grpc.node');

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

describe('call', function() {
  var channel;
  var server;
  before(function() {
    server = new grpc.Server();
    var port = server.addHttp2Port('localhost:0');
    server.start();
    channel = new grpc.Channel('localhost:' + port);
  });
  after(function() {
    server.shutdown();
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
    it('should fail with a closed channel', function() {
      var local_channel = new grpc.Channel('hostname');
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
  describe('addMetadata', function() {
    it('should succeed with a map from strings to string arrays', function() {
      var call = new grpc.Call(channel, 'method', getDeadline(1));
      assert.doesNotThrow(function() {
        call.addMetadata({'key': ['value']});
      });
      assert.doesNotThrow(function() {
        call.addMetadata({'key1': ['value1'], 'key2': ['value2']});
      });
    });
    it('should succeed with a map from strings to buffer arrays', function() {
      var call = new grpc.Call(channel, 'method', getDeadline(1));
      assert.doesNotThrow(function() {
        call.addMetadata({'key': [new Buffer('value')]});
      });
      assert.doesNotThrow(function() {
        call.addMetadata({'key1': [new Buffer('value1')],
                          'key2': [new Buffer('value2')]});
      });
    });
    it('should fail with other parameter types', function() {
      var call = new grpc.Call(channel, 'method', getDeadline(1));
      assert.throws(function() {
        call.addMetadata();
      });
      assert.throws(function() {
        call.addMetadata(null);
      }, TypeError);
      assert.throws(function() {
        call.addMetadata('value');
      }, TypeError);
      assert.throws(function() {
        call.addMetadata(5);
      }, TypeError);
    });
    it('should fail if invoke was already called', function(done) {
      var call = new grpc.Call(channel, 'method', getDeadline(1));
      call.invoke(function() {},
                  function() {done();},
                  0);
      assert.throws(function() {
        call.addMetadata({'key': ['value']});
      }, function(err) {
        return err.code === grpc.callError.ALREADY_INVOKED;
      });
      // Cancel to speed up the test
      call.cancel();
    });
  });
  describe('invoke', function() {
    it('should fail with fewer than 3 arguments', function() {
      var call = new grpc.Call(channel, 'method', getDeadline(1));
      assert.throws(function() {
        call.invoke();
      }, TypeError);
      assert.throws(function() {
        call.invoke(function() {});
      }, TypeError);
      assert.throws(function() {
        call.invoke(function() {},
                    function() {});
      }, TypeError);
    });
    it('should work with 2 args and an int', function(done) {
      assert.doesNotThrow(function() {
        var call = new grpc.Call(channel, 'method', getDeadline(1));
        call.invoke(function() {},
                    function() {done();},
                    0);
        // Cancel to speed up the test
        call.cancel();
      });
    });
    it('should reject incorrectly typed arguments', function() {
      var call = new grpc.Call(channel, 'method', getDeadline(1));
      assert.throws(function() {
        call.invoke(0, 0, 0);
      }, TypeError);
      assert.throws(function() {
        call.invoke(function() {},
                    function() {}, 'test');
      });
    });
  });
  describe('serverAccept', function() {
    it('should fail with fewer than 1 argument1', function() {
      var call = new grpc.Call(channel, 'method', getDeadline(1));
      assert.throws(function() {
        call.serverAccept();
      }, TypeError);
    });
    it('should return an error when called on a client Call', function() {
      var call = new grpc.Call(channel, 'method', getDeadline(1));
      assert.throws(function() {
        call.serverAccept(function() {});
      }, function(err) {
        return err.code === grpc.callError.NOT_ON_CLIENT;
      });
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
});
