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
var grpc = require('bindings')('grpc.node');

/**
 * This is used for testing functions with multiple asynchronous calls that
 * can happen in different orders. This should be passed the number of async
 * function invocations that can occur last, and each of those should call this
 * function's return value
 * @param {function()} done The function that should be called when a test is
 *     complete.
 * @param {number} count The number of calls to the resulting function if the
 *     test passes.
 * @return {function()} The function that should be called at the end of each
 *     sequence of asynchronous functions.
 */
function multiDone(done, count) {
  return function() {
    count -= 1;
    if (count <= 0) {
      done();
    }
  };
}

describe('end-to-end', function() {
  var server;
  var channel;
  before(function() {
    server = new grpc.Server();
    var port_num = server.addHttp2Port('0.0.0.0:0');
    server.start();
    channel = new grpc.Channel('localhost:' + port_num);
  });
  after(function() {
    server.shutdown();
  });
  it('should start and end a request without error', function(complete) {
    var done = multiDone(complete, 2);
    var deadline = new Date();
    deadline.setSeconds(deadline.getSeconds() + 3);
    var status_text = 'xyz';
    var call = new grpc.Call(channel,
                             'dummy_method',
                             Infinity);
    var client_batch = {};
    client_batch[grpc.opType.SEND_INITIAL_METADATA] = {};
    client_batch[grpc.opType.SEND_CLOSE_FROM_CLIENT] = true;
    client_batch[grpc.opType.RECV_INITIAL_METADATA] = true;
    client_batch[grpc.opType.RECV_STATUS_ON_CLIENT] = true;
    call.startBatch(client_batch, function(err, response) {
      assert.ifError(err);
      assert.deepEqual(response, {
        'send metadata': true,
        'client close': true,
        'metadata': {},
        'status': {
          'code': grpc.status.OK,
          'details': status_text,
          'metadata': {}
        }
      });
      done();
    });

    server.requestCall(function(err, call_details) {
      var new_call = call_details['new call'];
      assert.notEqual(new_call, null);
      var server_call = new_call.call;
      assert.notEqual(server_call, null);
      var server_batch = {};
      server_batch[grpc.opType.SEND_INITIAL_METADATA] = {};
      server_batch[grpc.opType.SEND_STATUS_FROM_SERVER] = {
        'metadata': {},
        'code': grpc.status.OK,
        'details': status_text
      };
      server_batch[grpc.opType.RECV_CLOSE_ON_SERVER] = true;
      server_call.startBatch(server_batch, function(err, response) {
        assert.ifError(err);
        assert.deepEqual(response, {
          'send metadata': true,
          'send status': true,
          'cancelled': false
        });
       done();
      });
    });
  });
  it('should successfully send and receive metadata', function(complete) {
    var done = multiDone(complete, 2);
    var deadline = new Date();
    deadline.setSeconds(deadline.getSeconds() + 3);
    var status_text = 'xyz';
    var call = new grpc.Call(channel,
                             'dummy_method',
                             Infinity);
    var client_batch = {};
    client_batch[grpc.opType.SEND_INITIAL_METADATA] = {
      'client_key': ['client_value']
    };
    client_batch[grpc.opType.SEND_CLOSE_FROM_CLIENT] = true;
    client_batch[grpc.opType.RECV_INITIAL_METADATA] = true;
    client_batch[grpc.opType.RECV_STATUS_ON_CLIENT] = true;
    call.startBatch(client_batch, function(err, response) {
      assert.ifError(err);
      assert(response['send metadata']);
      assert(response['client close']);
      assert(response.hasOwnProperty('metadata'));
      assert.strictEqual(response.metadata.server_key[0].toString(),
                         'server_value');
      assert.deepEqual(response.status, {'code': grpc.status.OK,
                                         'details': status_text,
                                         'metadata': {}});
      done();
    });

    server.requestCall(function(err, call_details) {
      var new_call = call_details['new call'];
      assert.notEqual(new_call, null);
      assert.strictEqual(new_call.metadata.client_key[0].toString(),
                         'client_value');
      var server_call = new_call.call;
      assert.notEqual(server_call, null);
      var server_batch = {};
      server_batch[grpc.opType.SEND_INITIAL_METADATA] = {
        'server_key': ['server_value']
      };
      server_batch[grpc.opType.SEND_STATUS_FROM_SERVER] = {
        'metadata': {},
        'code': grpc.status.OK,
        'details': status_text
      };
      server_batch[grpc.opType.RECV_CLOSE_ON_SERVER] = true;
      server_call.startBatch(server_batch, function(err, response) {
        assert.ifError(err);
        assert.deepEqual(response, {
          'send metadata': true,
          'send status': true,
          'cancelled': false
        });
       done();
      });
    });
  });
  it('should send and receive data without error', function(complete) {
    var req_text = 'client_request';
    var reply_text = 'server_response';
    var done = multiDone(complete, 2);
    var deadline = new Date();
    deadline.setSeconds(deadline.getSeconds() + 3);
    var status_text = 'success';
    var call = new grpc.Call(channel,
                             'dummy_method',
                             Infinity);
    var client_batch = {};
    client_batch[grpc.opType.SEND_INITIAL_METADATA] = {};
    client_batch[grpc.opType.SEND_MESSAGE] = new Buffer(req_text);
    client_batch[grpc.opType.SEND_CLOSE_FROM_CLIENT] = true;
    client_batch[grpc.opType.RECV_INITIAL_METADATA] = true;
    client_batch[grpc.opType.RECV_MESSAGE] = true;
    client_batch[grpc.opType.RECV_STATUS_ON_CLIENT] = true;
    call.startBatch(client_batch, function(err, response) {
      assert.ifError(err);
      assert(response['send metadata']);
      assert(response['client close']);
      assert.deepEqual(response.metadata, {});
      assert(response['send message']);
      assert.strictEqual(response.read.toString(), reply_text);
      assert.deepEqual(response.status, {'code': grpc.status.OK,
                                         'details': status_text,
                                         'metadata': {}});
      done();
    });

    server.requestCall(function(err, call_details) {
      var new_call = call_details['new call'];
      assert.notEqual(new_call, null);
      var server_call = new_call.call;
      assert.notEqual(server_call, null);
      var server_batch = {};
      server_batch[grpc.opType.SEND_INITIAL_METADATA] = {};
      server_batch[grpc.opType.RECV_MESSAGE] = true;
      server_call.startBatch(server_batch, function(err, response) {
        assert.ifError(err);
        assert(response['send metadata']);
        assert.strictEqual(response.read.toString(), req_text);
        var response_batch = {};
        response_batch[grpc.opType.SEND_MESSAGE] = new Buffer(reply_text);
        response_batch[grpc.opType.SEND_STATUS_FROM_SERVER] = {
          'metadata': {},
          'code': grpc.status.OK,
          'details': status_text
        };
        response_batch[grpc.opType.RECV_CLOSE_ON_SERVER] = true;
        server_call.startBatch(response_batch, function(err, response) {
          assert(response['send status']);
          assert(!response.cancelled);
          done();
        });
      });
    });
  });
});
