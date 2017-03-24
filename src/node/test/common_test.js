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

var common = require('../src/common.js');

var ProtoBuf = require('protobufjs');

var messages_proto = ProtoBuf.loadSync(
    __dirname + '/test_messages.proto');
messages_proto.resolveAll();

describe('Proto message long int serialize and deserialize', function() {
  var mplv = messages_proto.lookup('LongValues');
  var longSerialize = common.serializeCls(mplv);
  var longDeserialize = common.deserializeCls(mplv);
  var pos_value = '314159265358979';
  var neg_value = '-27182818284590';
  it('should preserve positive int64 values', function() {
    var serialized = longSerialize({int_64: pos_value});
    assert.strictEqual(longDeserialize(serialized).int_64.toString(),
                       pos_value);
  });
  it('should preserve negative int64 values', function() {
    var serialized = longSerialize({int_64: neg_value});
    assert.strictEqual(longDeserialize(serialized).int_64.toString(),
                       neg_value);
  });
  it('should preserve uint64 values', function() {
    var serialized = longSerialize({uint_64: pos_value});
    assert.strictEqual(longDeserialize(serialized).uint_64.toString(),
                       pos_value);
  });
  it('should preserve positive sint64 values', function() {
    var serialized = longSerialize({sint_64: pos_value});
    assert.strictEqual(longDeserialize(serialized).sint_64.toString(),
                       pos_value);
  });
  it('should preserve negative sint64 values', function() {
    var serialized = longSerialize({sint_64: neg_value});
    assert.strictEqual(longDeserialize(serialized).sint_64.toString(),
                       neg_value);
  });
  it('should preserve fixed64 values', function() {
    var serialized = longSerialize({fixed_64: pos_value});
    assert.strictEqual(longDeserialize(serialized).fixed_64.toString(),
                       pos_value);
  });
  it('should preserve positive sfixed64 values', function() {
    var serialized = longSerialize({sfixed_64: pos_value});
    assert.strictEqual(longDeserialize(serialized).sfixed_64.toString(),
                       pos_value);
  });
  it('should preserve negative sfixed64 values', function() {
    var serialized = longSerialize({sfixed_64: neg_value});
    assert.strictEqual(longDeserialize(serialized).sfixed_64.toString(),
                       neg_value);
  });
  it('should deserialize as a number with the right option set', function() {
    var longNumDeserialize = common.deserializeCls(mplv,
                                                   false, false, false);
    var serialized = longSerialize({int_64: pos_value});
    assert.strictEqual(typeof longDeserialize(serialized).int_64, 'string');
    /* With the longsAsStrings option disabled, long values are represented as
     * Long objects with 3 keys: low, high, and unsigned */
    assert.strictEqual(typeof longNumDeserialize(serialized).int_64, 'object');
  });
});
describe('Proto message bytes serialize and deserialize', function() {
  var mpsv = messages_proto.lookup('SequenceValues');
  var sequenceSerialize = common.serializeCls(mpsv);
  var sequenceDeserialize = common.deserializeCls(mpsv);
  var sequenceBase64Deserialize = common.deserializeCls(
      messages_proto.lookup('SequenceValues'), false, false, true);
  var buffer_val = new Buffer([0x69, 0xb7]);
  var base64_val = 'abc=';
  it('should preserve a buffer', function() {
    var serialized = sequenceSerialize({bytesField: buffer_val});
    var deserialized = sequenceDeserialize(serialized);
    assert.strictEqual(deserialized.bytesField.compare(buffer_val), 0);
  });
  it('should accept base64 encoded strings', function() {
    var serialized = sequenceSerialize({bytesField: base64_val});
    var deserialized = sequenceDeserialize(serialized);
    assert.strictEqual(deserialized.bytesField.compare(buffer_val), 0);
  });
  it('should output base64 encoded strings with an option set', function() {
    var serialized = sequenceSerialize({bytesField: base64_val});
    var deserialized = sequenceBase64Deserialize(serialized);
    assert.strictEqual(deserialized.bytesField, base64_val);
  });
  /* The next two tests are specific tests to verify that issue
   * https://github.com/grpc/grpc/issues/5174 has been fixed. They are skipped
   * because they will not pass until a protobuf.js release has been published
   * with a fix for https://github.com/dcodeIO/protobuf.js/issues/390 */
  it.skip('should serialize a repeated field as packed by default', function() {
    var expected_serialize = new Buffer([0x12, 0x01, 0x01, 0x0a]);
    var serialized = sequenceSerialize({repeatedField: [10]});
    assert.strictEqual(expected_serialize.compare(serialized), 0);
  });
  it.skip('should deserialize packed or unpacked repeated', function() {
    var serialized = new Buffer([0x12, 0x01, 0x01, 0x0a]);
    assert.doesNotThrow(function() {
      sequenceDeserialize(serialized);
    });
  });
});
