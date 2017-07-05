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
