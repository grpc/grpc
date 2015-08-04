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

var fs = require('fs');
var path = require('path');
var _ = require('lodash');
var grpc = require('..');
var testProto = grpc.load(__dirname + '/test.proto').grpc.testing;
var GoogleAuth = require('google-auth-library');

var assert = require('assert');

var AUTH_SCOPE = 'https://www.googleapis.com/auth/xapi.zoo';
var AUTH_SCOPE_RESPONSE = 'xapi.zoo';
var AUTH_USER = ('155450119199-3psnrh1sdr3d8cpj1v46naggf81mhdnk' +
    '@developer.gserviceaccount.com');
var COMPUTE_ENGINE_USER = ('155450119199-r5aaqa2vqoa9g5mv2m6s3m1l293rlmel' +
    '@developer.gserviceaccount.com');

/**
 * Create a buffer filled with size zeroes
 * @param {number} size The length of the buffer
 * @return {Buffer} The new buffer
 */
function zeroBuffer(size) {
  var zeros = new Buffer(size);
  zeros.fill(0);
  return zeros;
}

/**
 * Run the empty_unary test
 * @param {Client} client The client to test against
 * @param {function} done Callback to call when the test is completed. Included
 *     primarily for use with mocha
 */
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

/**
 * Run the large_unary test
 * @param {Client} client The client to test against
 * @param {function} done Callback to call when the test is completed. Included
 *     primarily for use with mocha
 */
function largeUnary(client, done) {
  var arg = {
    response_type: 'COMPRESSABLE',
    response_size: 314159,
    payload: {
      body: zeroBuffer(271828)
    }
  };
  var call = client.unaryCall(arg, function(err, resp) {
    assert.ifError(err);
    assert.strictEqual(resp.payload.type, 'COMPRESSABLE');
    assert.strictEqual(resp.payload.body.length, 314159);
  });
  call.on('status', function(status) {
    assert.strictEqual(status.code, grpc.status.OK);
    if (done) {
      done();
    }
  });
}

/**
 * Run the client_streaming test
 * @param {Client} client The client to test against
 * @param {function} done Callback to call when the test is completed. Included
 *     primarily for use with mocha
 */
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

/**
 * Run the server_streaming test
 * @param {Client} client The client to test against
 * @param {function} done Callback to call when the test is completed. Included
 *     primarily for use with mocha
 */
function serverStreaming(client, done) {
  var arg = {
    response_type: 'COMPRESSABLE',
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
    assert.strictEqual(value.payload.type, 'COMPRESSABLE');
    assert.strictEqual(value.payload.body.length,
                       arg.response_parameters[resp_index].size);
    resp_index += 1;
  });
  call.on('end', function() {
    assert.strictEqual(resp_index, 4);
    if (done) {
      done();
    }
  });
  call.on('status', function(status) {
    assert.strictEqual(status.code, grpc.status.OK);
  });
}

/**
 * Run the ping_pong test
 * @param {Client} client The client to test against
 * @param {function} done Callback to call when the test is completed. Included
 *     primarily for use with mocha
 */
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
      response_type: 'COMPRESSABLE',
      response_parameters: [
        {size: response_sizes[index]}
      ],
      payload: {body: zeroBuffer(payload_sizes[index])}
  });
  call.on('data', function(response) {
    assert.strictEqual(response.payload.type, 'COMPRESSABLE');
    assert.equal(response.payload.body.length, response_sizes[index]);
    index += 1;
    if (index === 4) {
      call.end();
    } else {
      call.write({
        response_type: 'COMPRESSABLE',
        response_parameters: [
          {size: response_sizes[index]}
        ],
        payload: {body: zeroBuffer(payload_sizes[index])}
      });
    }
  });
}

/**
 * Run the empty_stream test.
 * @param {Client} client The client to test against
 * @param {function} done Callback to call when the test is completed. Included
 *     primarily for use with mocha
 */
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

/**
 * Run the cancel_after_begin test.
 * @param {Client} client The client to test against
 * @param {function} done Callback to call when the test is completed. Included
 *     primarily for use with mocha
 */
function cancelAfterBegin(client, done) {
  var call = client.streamingInputCall(function(err, resp) {
    assert.strictEqual(err.code, grpc.status.CANCELLED);
    done();
  });
  call.cancel();
}

/**
 * Run the cancel_after_first_response test.
 * @param {Client} client The client to test against
 * @param {function} done Callback to call when the test is completed. Included
 *     primarily for use with mocha
 */
function cancelAfterFirstResponse(client, done) {
  var call = client.fullDuplexCall();
  call.write({
      response_type: 'COMPRESSABLE',
      response_parameters: [
        {size: 31415}
      ],
      payload: {body: zeroBuffer(27182)}
  });
  call.on('data', function(data) {
    call.cancel();
  });
  call.on('error', function(error) {
    assert.strictEqual(error.code, grpc.status.CANCELLED);
    done();
  });
}

function timeoutOnSleepingServer(client, done) {
  var deadline = new Date();
  deadline.setMilliseconds(deadline.getMilliseconds() + 1);
  var call = client.fullDuplexCall(null, deadline);
  call.write({
    payload: {body: zeroBuffer(27182)}
  });
  call.on('error', function(error) {
    assert.strictEqual(error.code, grpc.status.DEADLINE_EXCEEDED);
    done();
  });
}

/**
 * Run one of the authentication tests.
 * @param {string} expected_user The expected username in the response
 * @param {Client} client The client to test against
 * @param {?string} scope The scope to apply to the credentials
 * @param {function} done Callback to call when the test is completed. Included
 *     primarily for use with mocha
 */
function authTest(expected_user, scope, client, done) {
  (new GoogleAuth()).getApplicationDefault(function(err, credential) {
    assert.ifError(err);
    if (credential.createScopedRequired() && scope) {
      credential = credential.createScoped(scope);
    }
    client.updateMetadata = grpc.getGoogleAuthDelegate(credential);
    var arg = {
      response_type: 'COMPRESSABLE',
      response_size: 314159,
      payload: {
        body: zeroBuffer(271828)
      },
      fill_username: true,
      fill_oauth_scope: true
    };
    var call = client.unaryCall(arg, function(err, resp) {
      assert.ifError(err);
      assert.strictEqual(resp.payload.type, 'COMPRESSABLE');
      assert.strictEqual(resp.payload.body.length, 314159);
      assert.strictEqual(resp.username, expected_user);
      assert.strictEqual(resp.oauth_scope, AUTH_SCOPE_RESPONSE);
    });
    call.on('status', function(status) {
      assert.strictEqual(status.code, grpc.status.OK);
      if (done) {
        done();
      }
    });
  });
}

function oauth2Test(expected_user, scope, per_rpc, client, done) {
  (new GoogleAuth()).getApplicationDefault(function(err, credential) {
    assert.ifError(err);
    var arg = {
      fill_username: true,
      fill_oauth_scope: true
    };
    credential = credential.createScoped(scope);
    credential.getAccessToken(function(err, token) {
      assert.ifError(err);
      var updateMetadata = function(authURI, metadata, callback) {
        metadata = _.clone(metadata);
        if (metadata.Authorization) {
          metadata.Authorization = _.clone(metadata.Authorization);
        } else {
          metadata.Authorization = [];
        }
        metadata.Authorization.push('Bearer ' + token);
        callback(null, metadata);
      };
      var makeTestCall = function(error, client_metadata) {
        assert.ifError(error);
        var call = client.unaryCall(arg, function(err, resp) {
          assert.ifError(err);
          assert.strictEqual(resp.username, expected_user);
          assert.strictEqual(resp.oauth_scope, AUTH_SCOPE_RESPONSE);
        });
        call.on('status', function(status) {
          assert.strictEqual(status.code, grpc.status.OK);
          if (done) {
            done();
          }
        });
      };
      if (per_rpc) {
        updateMetadata('', {}, makeTestCall);
      } else {
        client.updateMetadata = updateMetadata;
        makeTestCall(null, {});
      }

    });
  });
}

/**
 * Map from test case names to test functions
 */
var test_cases = {
  empty_unary: emptyUnary,
  large_unary: largeUnary,
  client_streaming: clientStreaming,
  server_streaming: serverStreaming,
  ping_pong: pingPong,
  empty_stream: emptyStream,
  cancel_after_begin: cancelAfterBegin,
  cancel_after_first_response: cancelAfterFirstResponse,
  timeout_on_sleeping_server: timeoutOnSleepingServer,
  compute_engine_creds: _.partial(authTest, COMPUTE_ENGINE_USER, null),
  service_account_creds: _.partial(authTest, AUTH_USER, AUTH_SCOPE),
  jwt_token_creds: _.partial(authTest, AUTH_USER, null),
  oauth2_auth_token: _.partial(oauth2Test, AUTH_USER, AUTH_SCOPE, false),
  per_rpc_creds: _.partial(oauth2Test, AUTH_USER, AUTH_SCOPE, true)
};

/**
 * Execute a single test case.
 * @param {string} address The address of the server to connect to, in the
 *     format 'hostname:port'
 * @param {string} host_overrirde The hostname of the server to use as an SSL
 *     override
 * @param {string} test_case The name of the test case to run
 * @param {bool} tls Indicates that a secure channel should be used
 * @param {function} done Callback to call when the test is completed. Included
 *     primarily for use with mocha
 */
function runTest(address, host_override, test_case, tls, test_ca, done) {
  // TODO(mlumish): enable TLS functionality
  var options = {};
  var creds;
  if (tls) {
    var ca_path;
    if (test_ca) {
      ca_path = path.join(__dirname, '../test/data/ca.pem');
    } else {
      ca_path = process.env.SSL_CERT_FILE;
    }
    var ca_data = fs.readFileSync(ca_path);
    creds = grpc.Credentials.createSsl(ca_data);
    if (host_override) {
      options['grpc.ssl_target_name_override'] = host_override;
    }
  } else {
    creds = grpc.Credentials.createInsecure();
  }
  var client = new testProto.TestService(address, creds, options);

  test_cases[test_case](client, done);
}

if (require.main === module) {
  var parseArgs = require('minimist');
  var argv = parseArgs(process.argv, {
    string: ['server_host', 'server_host_override', 'server_port', 'test_case',
             'use_tls', 'use_test_ca']
  });
  runTest(argv.server_host + ':' + argv.server_port, argv.server_host_override,
          argv.test_case, argv.use_tls === 'true', argv.use_test_ca === 'true',
          function () {
            console.log('OK:', argv.test_case);
          });
}

/**
 * See docs for runTest
 */
exports.runTest = runTest;
