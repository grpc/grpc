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
var testProto = grpc.load(__dirname + '/test.proto').grpc.testing;

var assert = require('assert');

function zeroBuffer(size) {
  var zeros = new Buffer(size);
  zeros.fill(0);
  return zeros;
}

function emptyUnary(client, done) {
  var call = client.emptyCall({}, function(err, resp) {
    assert.ifError(err);
  });
  call.on('status', function(status) {
    assert.strictEqual(status.code, grpc.status.OK);
    if (done) {
      done();
    }
  });
}

function largeUnary(client, done) {
  var arg = {
    response_type: testProto.PayloadType.COMPRESSABLE,
    response_size: 314159,
    payload: {
      body: zeroBuffer(271828)
    }
  };
  var call = client.unaryCall(arg, function(err, resp) {
    assert.ifError(err);
    assert.strictEqual(resp.payload.type, testProto.PayloadType.COMPRESSABLE);
    assert.strictEqual(resp.payload.body.limit - resp.payload.body.offset,
                       314159);
  });
  call.on('status', function(status) {
    assert.strictEqual(status.code, grpc.status.OK);
    if (done) {
      done();
    }
  });
}

function clientStreaming(client, done) {
  var call = client.streamingInputCall(function(err, resp) {
    assert.ifError(err);
    assert.strictEqual(resp.aggregated_payload_size, 74922);
  });
  call.on('status', function(status) {
    assert.strictEqual(status.code, grpc.status.OK);
    if (done) {
      done();
    }
  });
  var payload_sizes = [27182, 8, 1828, 45904];
  for (var i = 0; i < payload_sizes.length; i++) {
    call.write({payload: {body: zeroBuffer(payload_sizes[i])}});
  }
  call.end();
}

function serverStreaming(client, done) {
  var arg = {
    response_type: testProto.PayloadType.COMPRESSABLE,
    response_parameters: [
      {size: 31415},
      {size: 9},
      {size: 2653},
      {size: 58979}
    ]
  };
  var call = client.streamingOutputCall(arg);
  var resp_index = 0;
  call.on('data', function(value) {
    assert(resp_index < 4);
    assert.strictEqual(value.payload.type, testProto.PayloadType.COMPRESSABLE);
    assert.strictEqual(value.payload.body.limit - value.payload.body.offset,
                       arg.response_parameters[resp_index].size);
    resp_index += 1;
  });
  call.on('status', function(status) {
    assert.strictEqual(resp_index, 4);
    assert.strictEqual(status.code, grpc.status.OK);
    if (done) {
      done();
    }
  });
}

function pingPong(client, done) {
  var payload_sizes = [27182, 8, 1828, 45904];
  var response_sizes = [31415, 9, 2653, 58979];
  var call = client.fullDuplexCall();
  call.on('status', function(status) {
    assert.strictEqual(status.code, grpc.status.OK);
    if (done) {
      done();
    }
  });
  var index = 0;
  call.write({
      response_type: testProto.PayloadType.COMPRESSABLE,
      response_parameters: [
        {size: response_sizes[index]}
      ],
      payload: {body: zeroBuffer(payload_sizes[index])}
  });
  call.on('data', function(response) {
    assert.strictEqual(response.payload.type,
                       testProto.PayloadType.COMPRESSABLE);
    assert.equal(response.payload.body.limit - response.payload.body.offset,
                 response_sizes[index]);
    index += 1;
    if (index == 4) {
      call.end();
    } else {
      call.write({
        response_type: testProto.PayloadType.COMPRESSABLE,
        response_parameters: [
          {size: response_sizes[index]}
        ],
        payload: {body: zeroBuffer(payload_sizes[index])}
      });
    }
  });
}

function emptyStream(client, done) {
  var call = client.fullDuplexCall();
  call.on('status', function(status) {
    assert.strictEqual(status.code, grpc.status.OK);
    if (done) {
      done();
    }
  });
  call.on('data', function(value) {
    assert.fail(value, null, 'No data should have been received', '!==');
  });
  call.end();
}

var test_cases = {
  empty_unary: emptyUnary,
  large_unary: largeUnary,
  client_streaming: clientStreaming,
  server_streaming: serverStreaming,
  ping_pong: pingPong,
  empty_stream: emptyStream
};

/**
 * Execute a single test case.
 * @param {string} address The address of the server to connect to, in the
 *     format "hostname:port"
 * @param {string} host_overrirde The hostname of the server to use as an SSL
 *     override
 * @param {string} test_case The name of the test case to run
 * @param {bool} tls Indicates that a secure channel should be used
 * @param {function} done Callback to call when the test is completed. Included
 *     primarily for use with mocha
 */
function runTest(address, host_override, test_case, tls, done) {
  // TODO(mlumish): enable TLS functionality
  // TODO(mlumish): fix namespaces and service name
  var client = new testProto.TestService(address);

  test_cases[test_case](client, done);
}

if (require.main === module) {
  var parseArgs = require('minimist');
  var argv = parseArgs(process.argv, {
    string: ['server_host', 'server_host_override', 'server_port', 'test_case',
             'use_tls', 'use_test_ca']
  });
  runTest(argv.server_host + ':' + argv.server_port, argv.server_host_override,
          argv.test_case, argv.use_tls === 'true');
}

/**
 * See docs for runTest
 */
exports.runTest = runTest;
