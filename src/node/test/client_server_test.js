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
var fs = require('fs');
var path = require('path');
var grpc = require('bindings')('grpc.node');
var Server = require('../src/server');
var client = require('../src/client');
var common = require('../src/common');

var ca_path = path.join(__dirname, 'data/ca.pem');

var key_path = path.join(__dirname, 'data/server1.key');

var pem_path = path.join(__dirname, 'data/server1.pem');

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

/**
 * Responds to every request with the same data as a response
 * @param {Stream} stream
 */
function echoHandler(stream) {
  stream.pipe(stream);
}

/**
 * Responds to every request with an error status
 * @param {Stream} stream
 */
function errorHandler(stream) {
  throw {
    'code' : grpc.status.UNIMPLEMENTED,
    'details' : 'error details'
  };
}

/**
 * Wait for a cancellation instead of responding
 * @param {Stream} stream
 */
function cancelHandler(stream) {
  // do nothing
}

/**
 * Serialize a string to a Buffer
 * @param {string} value The string to serialize
 * @return {Buffer} The serialized value
 */
function stringSerialize(value) {
  return new Buffer(value);
}

/**
 * Deserialize a Buffer to a string
 * @param {Buffer} buffer The buffer to deserialize
 * @return {string} The string value of the buffer
 */
function stringDeserialize(buffer) {
}

describe('echo client', function() {
  var server;
  var channel;
  before(function() {
    server = new Server();
    var port_num = server.bind('0.0.0.0:0');
    server.register('echo', echoHandler);
    server.register('error', errorHandler);
    server.register('cancellation', cancelHandler);
    server.start();

    channel = new grpc.Channel('localhost:' + port_num);
  });
  after(function() {
    server.shutdown();
  });
  it('should receive echo responses', function(done) {
    var messages = ['echo1', 'echo2', 'echo3', 'echo4'];
    var stream = client.makeRequest(
        channel,
        'echo',
        stringSerialize,
        stringDeserialize);
    for (var i = 0; i < messages.length; i++) {
      stream.write(messages[i]);
    }
    var index = 0;
    stream.on('data', function(chunk) {
      assert.equal(messages[index], chunk.toString());
      index += 1;
    });
    stream.on('end', function() {
      done();
    });
  });
  it('should get an error status that the server throws', function(done) {
    var stream = client.makeRequest(
        channel,
        'error',
        null,
        getDeadline(1));

    stream.on('data', function() {});
    stream.write(new Buffer('test'));
    stream.end();
    stream.on('status', function(status) {
      assert.equal(status.code, grpc.status.UNIMPLEMENTED);
      assert.equal(status.details, 'error details');
      done();
    });
  });
  it('should be able to cancel a call', function(done) {
    var stream = client.makeRequest(
        channel,
        'cancellation',
        null,
        getDeadline(1));

    stream.cancel();
    stream.on('status', function(status) {
      assert.equal(status.code, grpc.status.CANCELLED);
      done();
    });
  });
});
/* TODO(mlumish): explore options for reducing duplication between this test
 * and the insecure echo client test */
describe('secure echo client', function() {
  var server;
  var channel;
  before(function(done) {
    fs.readFile(ca_path, function(err, ca_data) {
      assert.ifError(err);
      fs.readFile(key_path, function(err, key_data) {
        assert.ifError(err);
        fs.readFile(pem_path, function(err, pem_data) {
          assert.ifError(err);
          var creds = grpc.Credentials.createSsl(ca_data);
          var server_creds = grpc.ServerCredentials.createSsl(null,
                                                              key_data,
                                                              pem_data);

          server = new Server({'credentials' : server_creds});
          var port_num = server.bind('0.0.0.0:0', true);
          server.register('echo', echoHandler);
          server.start();

          channel = new grpc.Channel('localhost:' + port_num, {
            'grpc.ssl_target_name_override' : 'foo.test.google.com',
            'credentials' : creds
          });
          done();
        });
      });
    });
  });
  after(function() {
    server.shutdown();
  });
  it('should recieve echo responses', function(done) {
    var messages = ['echo1', 'echo2', 'echo3', 'echo4'];
    var stream = client.makeRequest(
        channel,
        'echo',
        stringSerialize,
        stringDeserialize);
    for (var i = 0; i < messages.length; i++) {
      stream.write(messages[i]);
    }
    var index = 0;
    stream.on('data', function(chunk) {
      assert.equal(messages[index], chunk.toString());
      index += 1;
    });
    stream.on('end', function() {
      server.shutdown();
      done();
    });
  });
});
