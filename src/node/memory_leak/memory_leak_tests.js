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

const assert = require('assert');
const async = require('async');
const _ = require('lodash');

const grpc = require('..');

const getMathServer = require('../test/math/math_server');

const NUM_RUNS = 30;
const DATA_POINTS = 10;

/**
 * This function attempts to determine approximately whether a given operation
 * leaks any memory. In theory, if a single fixed operation leaks memory, then
 * the increase in memory usage should be approximately proportional to the
 * number of times that operation is run. We estimate this by calling the
 * operation N times and recording the increase in memory usage (if any), then
 * calling it again another 2 * N times, again recording the increase in memory
 * usage relative to the starting value. To account for noise, we make a
 * conservative estimate and say that it leaked if the additional memory used
 * after a total of 3 * N calls is at least twice the additional memory used
 * after just N calls. We try to choose N that is large enough that its
 * operations are the dominant factor, and small enough that if there is a leak
 * the process will not run out of memory.
 * @param {function(function(Error))} checkFun The function to check for leaks
 * @param {function(Error)} cb The callback to pass the result to
 */
function doesLeak(checkFun, cb) {
  function wrappedCheckFun(id, next) {
    checkFun(next);
  }
  const startRss = process.memoryUsage().rss;
  function runManyAndCheckUsage(id, next) {
    async.times(NUM_RUNS, wrappedCheckFun, function(err) {
      if (err) {
        next(err);
        return;
      }
      global.gc();
      const rssDiff = process.memoryUsage().rss - startRss;
      next(null, rssDiff);
    });
  }
  async.times(DATA_POINTS, runManyAndCheckUsage, function(error, diffs) {
    if (error) {
      cb(error);
      return;
    }
    cb(null, _.every(diffs, function(diff, index, diffs) {
      return diff > index * diffs[0];
    }));
  });
}

function assertDoesNotLeakAsync(checkFun, done) {
  doesLeak(checkFun, function(error, isLeaky) {
    if (error) {
      throw error;
    }
    assert.ok(!isLeaky);
    done();
  });
}

describe('Memory leaks', function() {
  describe('Server', function() {
    it('Should not leak with tryShutdown', function(done) {
      function createServer(next) {
        let server = new grpc.Server();
        server.bind('localhost:0', grpc.ServerCredentials.createInsecure());
        server.start();
        server.tryShutdown(function() {
          next();
        });
      }
      assertDoesNotLeakAsync(createServer, done);
    });
  });
  describe('Call', function() {
    var server;
    var client;
    before(function() {
      const test_proto = grpc.load(__dirname + '/../test/test_service.proto');
      var TestService = test_proto.TestService;
      server = new grpc.Server();
      server.addService(TestService.service, {
        unary: function(call, cb) {
          call.sendMetadata(call.metadata);
          cb(null, {});
        },
        clientStream: function(stream, cb){
          stream.on('data', function(data) {});
          stream.on('end', function() {
            stream.sendMetadata(stream.metadata);
            cb(null, {});
          });
        },
        serverStream: function(stream) {
          stream.sendMetadata(stream.metadata);
          stream.write({});
          stream.write({});
          stream.end();
        },
        bidiStream: function(stream) {
          stream.on('data', function(data) {
            stream.write({});
          });
          stream.on('end', function() {
            stream.sendMetadata(stream.metadata);
            stream.end();
          });
        }
      });
      const port = server.bind('localhost:0',
                               grpc.ServerCredentials.createInsecure());
      client = new TestService('localhost:' + port,
                               grpc.credentials.createInsecure());
      server.start();
    });
    after(function(done) {
      server.tryShutdown(done);
    });
    it('Unary call should not leak', function(done) {
      function makeCall(next) {
        client.unary({}, function(err, value) {
          next(err);
        });
      }
      assertDoesNotLeakAsync(makeCall, done);
    });
    it('Client streaming call should not leak', function(done) {
      function makeCall(next) {
        let call = client.clientStream(function(err, value) {
          next(err);
        });
        call.write({});
        call.write({});
        call.end();
      }
      assertDoesNotLeakAsync(makeCall, done);
    });
    it('Server streaming call should not leak', function(done) {
      function makeCall(next) {
        let call = client.serverStream({});
        call.on('data', function(data) {
        });
        call.on('status', function(status) {
          if (status.code === grpc.status.OK) {
            next(null);
          } else {
            next(new Error(status.details));
          }
        });
      }
      assertDoesNotLeakAsync(makeCall, done);
    });
    it('Bidi streaming call should not leak', function(done) {
      function makeCall(next) {
        let call = client.bidiStream();
        call.on('data', function(data) {
        });
        call.on('status', function(status) {
          if (status.code === grpc.status.OK) {
            next(null);
          } else {
            next(new Error(status.details));
          }
        });
        call.write({});
        call.write({});
        call.end();
      }
      assertDoesNotLeakAsync(makeCall, done);
    });
    it('Should not leak with metadata', function(done) {
      function makeCall(next) {
        const metadata = new grpc.Metadata();
        metadata.add('key', 'value');
        metadata.add('key-bin', new Buffer('value'));
        client.unary({}, metadata, function(err, value) {
          next(err);
        });
      }
      assertDoesNotLeakAsync(makeCall, done);
    });
  });
});
