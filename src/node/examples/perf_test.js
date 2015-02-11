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

var grpc = require('..');
var testProto = grpc.load(__dirname + '/../interop/test.proto').grpc.testing;
var _ = require('underscore');
var interop_server = require('../interop/interop_server.js');

function runTest(iterations, callback) {
  var testServer = interop_server.getServer(0, false);
  testServer.server.listen();
  var client = new testProto.TestService('localhost:' + testServer.port);

  function runIterations(finish) {
    var start = process.hrtime();
    var intervals = [];
    var pending = iterations;
    function next(i) {
      if (i >= iterations) {
        testServer.server.shutdown();
        var totalDiff = process.hrtime(start);
        finish({
          total: totalDiff[0] * 1000000 + totalDiff[1] / 1000,
          intervals: intervals
        });
      } else{
        var deadline = new Date();
        deadline.setSeconds(deadline.getSeconds() + 3);
        var startTime = process.hrtime();
        client.emptyCall({}, function(err, resp) {
          var timeDiff = process.hrtime(startTime);
          intervals[i] = timeDiff[0] * 1000000 + timeDiff[1] / 1000;
          next(i+1);
        }, {}, deadline);
      }
    }
    next(0);
  }

  function warmUp(num) {
    var pending = num;
    for (var i = 0; i < num; i++) {
      (function(i) {
        client.emptyCall({}, function(err, resp) {
          pending--;
          if (pending === 0) {
            runIterations(callback);
          }
        });
      })(i);
    }
  }
  warmUp(100);
}

if (require.main === module) {
  var count;
  if (process.argv.length >= 3) {
    count = process.argv[2];
  } else {
    count = 100;
  }
  runTest(count, function(results) {
    console.log('count:', count);
    console.log('total time:', results.total, 'us');
    console.log('min latency:', _.min(results.intervals), 'us');
    console.log('max latency:', _.max(results.intervals), 'us');
    console.log('average latency:', _.reduce(results.intervals, function(a, b){
      return a+b;
    }) / count, 'us');
  });
}

module.exports = runTest;
