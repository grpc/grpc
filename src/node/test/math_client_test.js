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
var client = require('../surface_client.js');
var ProtoBuf = require('protobufjs');
var port_picker = require('../port_picker');

var builder = ProtoBuf.loadProtoFile(__dirname + '/../examples/math.proto');
var math = builder.build('math');

/**
 * Get a function that deserializes a specific type of protobuf.
 * @param {function()} cls The constructor of the message type to deserialize
 * @return {function(Buffer):cls} The deserialization function
 */
function deserializeCls(cls) {
  /**
   * Deserialize a buffer to a message object
   * @param {Buffer} arg_buf The buffer to deserialize
   * @return {cls} The resulting object
   */
  return function deserialize(arg_buf) {
    return cls.decode(arg_buf);
  };
}

/**
 * Serialize an object to a buffer
 * @param {*} arg The object to serialize
 * @return {Buffer} The serialized object
 */
function serialize(arg) {
  return new Buffer(arg.encode().toBuffer());
}

/**
 * Sends a Div request on the channel.
 * @param {client.Channel} channel The channel on which to make the request
 * @param {DivArg} argument The argument to the call. Should be serializable
 *     with serialize
 * @param {function(?Error, value=)} The callback to for when the response is
 *     received
 * @param {array=} Array of metadata key/value pairs to add to the call
 * @param {(number|Date)=} deadline The deadline for processing this request.
 *     Defaults to infinite future
 * @return {EventEmitter} An event emitter for stream related events
 */
var div = client.makeUnaryRequestFunction(
    '/Math/Div',
    serialize,
    deserializeCls(math.DivReply));

/**
 * Sends a Fib request on the channel.
 * @param {client.Channel} channel The channel on which to make the request
 * @param {*} argument The argument to the call. Should be serializable with
 *     serialize
 * @param {array=} Array of metadata key/value pairs to add to the call
 * @param {(number|Date)=} deadline The deadline for processing this request.
 *     Defaults to infinite future
 * @return {EventEmitter} An event emitter for stream related events
 */
var fib = client.makeServerStreamRequestFunction(
    '/Math/Fib',
    serialize,
    deserializeCls(math.Num));

/**
 * Sends a Sum request on the channel.
 * @param {client.Channel} channel The channel on which to make the request
 * @param {function(?Error, value=)} The callback to for when the response is
 *     received
 * @param {array=} Array of metadata key/value pairs to add to the call
 * @param {(number|Date)=} deadline The deadline for processing this request.
 *     Defaults to infinite future
 * @return {EventEmitter} An event emitter for stream related events
 */
var sum = client.makeClientStreamRequestFunction(
    '/Math/Sum',
    serialize,
    deserializeCls(math.Num));

/**
 * Sends a DivMany request on the channel.
 * @param {client.Channel} channel The channel on which to make the request
 * @param {array=} Array of metadata key/value pairs to add to the call
 * @param {(number|Date)=} deadline The deadline for processing this request.
 *     Defaults to infinite future
 * @return {EventEmitter} An event emitter for stream related events
 */
var divMany = client.makeBidiStreamRequestFunction(
    '/Math/DivMany',
    serialize,
    deserializeCls(math.DivReply));

/**
 * Channel to use to make requests to a running server.
 */
var channel;

/**
 * Server to test against
 */
var server = require('../examples/math_server.js');


describe('Math client', function() {
  before(function(done) {
    port_picker.nextAvailablePort(function(port) {
      server.bind(port).listen();
      channel = new client.Channel(port);
      done();
    });
  });
  after(function() {
    server.shutdown();
  });
  it('should handle a single request', function(done) {
    var arg = new math.DivArgs({dividend: 7, divisor: 4});
    var call = div(channel, arg, function handleDivResult(err, value) {
      assert.ifError(err);
      assert.equal(value.get('quotient'), 1);
      assert.equal(value.get('remainder'), 3);
    });
    call.on('status', function checkStatus(status) {
      assert.strictEqual(status.code, client.status.OK);
      done();
    });
  });
  it('should handle a server streaming request', function(done) {
    var arg = new math.FibArgs({limit: 7});
    var call = fib(channel, arg);
    var expected_results = [1, 1, 2, 3, 5, 8, 13];
    var next_expected = 0;
    call.on('data', function checkResponse(value) {
      assert.equal(value.get('num'), expected_results[next_expected]);
      next_expected += 1;
    });
    call.on('status', function checkStatus(status) {
      assert.strictEqual(status.code, client.status.OK);
      done();
    });
  });
  it('should handle a client streaming request', function(done) {
    var call = sum(channel, function handleSumResult(err, value) {
      assert.ifError(err);
      assert.equal(value.get('num'), 21);
    });
    for (var i = 0; i < 7; i++) {
      call.write(new math.Num({'num': i}));
    }
    call.end();
    call.on('status', function checkStatus(status) {
      assert.strictEqual(status.code, client.status.OK);
      done();
    });
  });
  it('should handle a bidirectional streaming request', function(done) {
    function checkResponse(index, value) {
      assert.equal(value.get('quotient'), index);
      assert.equal(value.get('remainder'), 1);
    }
    var call = divMany(channel);
    var response_index = 0;
    call.on('data', function(value) {
      checkResponse(response_index, value);
      response_index += 1;
    });
    for (var i = 0; i < 7; i++) {
      call.write(new math.DivArgs({dividend: 2 * i + 1, divisor: 2}));
    }
    call.end();
    call.on('status', function checkStatus(status) {
      assert.strictEqual(status.code, client.status.OK);
      done();
    });
  });
});
