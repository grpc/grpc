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

var grpc = require('..');
var math = grpc.load(__dirname + '/../../proto/math/math.proto').math;


/**
 * Client to use to make requests to a running server.
 */
var math_client;

/**
 * Server to test against
 */
var getServer = require('./math/math_server.js');

var server = getServer();

describe('Async functionality', function() {
  before(function(done) {
    var port_num = server.bind('0.0.0.0:0',
                               grpc.ServerCredentials.createInsecure());
    server.start();
    math_client = new math.Math('localhost:' + port_num,
                                grpc.credentials.createInsecure());
    done();
  });
  after(function() {
    grpc.closeClient(math_client);
    server.forceShutdown();
  });
  it('should not hang', function(done) {
    var chunkCount=0;
    var call = math_client.sum(function handleSumResult(err, value) {
      assert.ifError(err);
      assert.equal(value.num, chunkCount);
    });

    var path = require('path');
    var fs = require('fs');
    var fileToRead = path.join(__dirname, 'numbers.txt');
    var readStream = fs.createReadStream(fileToRead);

    readStream.once('readable', function () {
      readStream.on('data', function (chunk) {
        call.write({'num': 1});
        chunkCount += 1;
      });

      readStream.on('end', function () {
        call.end();
      });

      readStream.on('error', function (error) {
      });
    });

    call.on('status', function checkStatus(status) {
      assert.strictEqual(status.code, grpc.status.OK);
      done();
    });
  });
});
