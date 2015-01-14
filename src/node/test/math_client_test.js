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
var ProtoBuf = require('protobufjs');
var port_picker = require('../port_picker');

var builder = ProtoBuf.loadProtoFile(__dirname + '/../examples/math.proto');
var math = builder.build('math');

var client = require('../surface_client.js');
var makeConstructor = client.makeClientConstructor;
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
 * Get a function that serializes objects to a buffer by protobuf class.
 * @param {function()} Cls The constructor of the message type to serialize
 * @return {function(Cls):Buffer} The serialization function
 */
function serializeCls(Cls) {
  /**
   * Serialize an object to a Buffer
   * @param {Object} arg The object to serialize
   * @return {Buffer} The serialized object
   */
  return function serialize(arg) {
    return new Buffer(new Cls(arg).encode().toBuffer());
  };
}

/* This function call creates a client constructor for clients that expose the
 * four specified methods. This specifies how to serialize messages that the
 * client sends and deserialize messages that the server sends, and whether the
 * client or the server will send a stream of messages, for each method. This
 * also specifies a prefix tha twill be added to method names when sending them
 * on the wire. This function call and all of the preceding code in this file
 * are intended to approximate what the generated code will look like for the
 * math client */
var MathClient = makeConstructor({
  Div: {
    serialize: serializeCls(math.DivArgs),
    deserialize: deserializeCls(math.DivReply),
    client_stream: false,
    server_stream: false
  },
  Fib: {
    serialize: serializeCls(math.FibArgs),
    deserialize: deserializeCls(math.Num),
    client_stream: false,
    server_stream: true
  },
  Sum: {
    serialize: serializeCls(math.Num),
    deserialize: deserializeCls(math.Num),
    client_stream: true,
    server_stream: false
  },
  DivMany: {
    serialize: serializeCls(math.DivArgs),
    deserialize: deserializeCls(math.DivReply),
    client_stream: true,
    server_stream: true
  }
}, '/Math/');

/**
 * Channel to use to make requests to a running server.
 */
var math_client;

/**
 * Server to test against
 */
var server = require('../examples/math_server.js');


describe('Math client', function() {
  before(function(done) {
    port_picker.nextAvailablePort(function(port) {
      server.bind(port).listen();
      math_client = new MathClient(port);
      done();
    });
  });
  after(function() {
    server.shutdown();
  });
  it('should handle a single request', function(done) {
    var arg = {dividend: 7, divisor: 4};
    var call = math_client.Div(arg, function handleDivResult(err, value) {
      assert.ifError(err);
      assert.equal(value.quotient, 1);
      assert.equal(value.remainder, 3);
    });
    call.on('status', function checkStatus(status) {
      assert.strictEqual(status.code, client.status.OK);
      done();
    });
  });
  it('should handle a server streaming request', function(done) {
    var call = math_client.Fib({limit: 7});
    var expected_results = [1, 1, 2, 3, 5, 8, 13];
    var next_expected = 0;
    call.on('data', function checkResponse(value) {
      assert.equal(value.num, expected_results[next_expected]);
      next_expected += 1;
    });
    call.on('status', function checkStatus(status) {
      assert.strictEqual(status.code, client.status.OK);
      done();
    });
  });
  it('should handle a client streaming request', function(done) {
    var call = math_client.Sum(function handleSumResult(err, value) {
      assert.ifError(err);
      assert.equal(value.num, 21);
    });
    for (var i = 0; i < 7; i++) {
      call.write({'num': i});
    }
    call.end();
    call.on('status', function checkStatus(status) {
      assert.strictEqual(status.code, client.status.OK);
      done();
    });
  });
  it('should handle a bidirectional streaming request', function(done) {
    function checkResponse(index, value) {
      assert.equal(value.quotient, index);
      assert.equal(value.remainder, 1);
    }
    var call = math_client.DivMany();
    var response_index = 0;
    call.on('data', function(value) {
      checkResponse(response_index, value);
      response_index += 1;
    });
    for (var i = 0; i < 7; i++) {
      call.write({dividend: 2 * i + 1, divisor: 2});
    }
    call.end();
    call.on('status', function checkStatus(status) {
      assert.strictEqual(status.code, client.status.OK);
      done();
    });
  });
});
