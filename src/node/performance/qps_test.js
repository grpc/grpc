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

/**
 * This script runs a QPS test. It sends requests for a specified length of time
 * with a specified number pending at any one time. It then outputs the measured
 * QPS. Usage:
 * node qps_test.js [--concurrent=count] [--time=seconds]
 * concurrent defaults to 100 and time defaults to 10
 */

'use strict';

var async = require('async');
var parseArgs = require('minimist');

var grpc = require('..');
var testProto = grpc.load(__dirname + '/../interop/test.proto').grpc.testing;
var interop_server = require('../interop/interop_server.js');

/**
 * Runs the QPS test. Sends requests constantly for the given number of seconds,
 * and keeps concurrent_calls requests pending at all times. When the test ends,
 * the callback is called with the number of calls that completed within the
 * time limit.
 * @param {number} concurrent_calls The number of calls to have pending
 *     simultaneously
 * @param {number} seconds The number of seconds to run the test for
 * @param {function(Error, number)} callback Callback for test completion
 */
function runTest(concurrent_calls, seconds, callback) {
  var testServer = interop_server.getServer(0, false);
  testServer.server.start();
  var client = new testProto.TestService('localhost:' + testServer.port,
                                         grpc.credentials.createInsecure());

  var warmup_num = 100;

  /**
   * Warms up the client to avoid counting startup time in the test result
   * @param {function(Error)} callback Called when warmup is complete
   */
  function warmUp(callback) {
    var pending = warmup_num;
    function startCall() {
      client.emptyCall({}, function(err, resp) {
        if (err) {
          callback(err);
          return;
        }
        pending--;
        if (pending === 0) {
          callback(null);
        }
      });
    }
    for (var i = 0; i < warmup_num; i++) {
      startCall();
    }
  }
  /**
   * Run the QPS test. Starts concurrent_calls requests, then starts a new
   * request whenever one completes until time runs out.
   * @param {function(Error, number)} callback Called when the test is complete.
   *     The second argument is the number of calls that finished within the
   *     time limit
   */
  function run(callback) {
    var running = 0;
    var count = 0;
    var start = process.hrtime();
    function responseCallback(err, resp) {
      if (process.hrtime(start)[0] < seconds) {
        count += 1;
        client.emptyCall({}, responseCallback);
      } else {
        running -= 1;
        if (running <= 0) {
          callback(null, count);
        }
      }
    }
    for (var i = 0; i < concurrent_calls; i++) {
      running += 1;
      client.emptyCall({}, responseCallback);
    }
  }
  async.waterfall([warmUp, run], function(err, count) {
      testServer.server.shutdown();
      callback(err, count);
    });
}

if (require.main === module) {
  var argv = parseArgs(process.argv.slice(2), {
    default: {'concurrent': 100,
              'time': 10}
  });
  runTest(argv.concurrent, argv.time, function(err, count) {
    if (err) {
      throw err;
    }
    console.log('Concurrent calls:', argv.concurrent);
    console.log('Time:', argv.time, 'seconds');
    console.log('QPS:', (count/argv.time));
  });
}
