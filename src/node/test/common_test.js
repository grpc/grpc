/*
 *
 * Copyright 2015, gRPC authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
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

var common = require('../src/common.js');

var ProtoBuf = require('protobufjs');

var messages_proto = ProtoBuf.loadProtoFile(
    __dirname + '/test_messages.proto').build();

describe('Proto message long int serialize and deserialize', function() {
  var longSerialize = common.serializeCls(messages_proto.LongValues);
  var longDeserialize = common.deserializeCls(messages_proto.LongValues);
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
    var longNumDeserialize = common.deserializeCls(messages_proto.LongValues,
                                                   false, false);
    var serialized = longSerialize({int_64: pos_value});
    assert.strictEqual(typeof longDeserialize(serialized).int_64, 'string');
    /* With the longsAsStrings option disabled, long values are represented as
     * objects with 3 keys: low, high, and unsigned */
    assert.strictEqual(typeof longNumDeserialize(serialized).int_64, 'object');
  });
});
describe('Proto message bytes serialize and deserialize', function() {
  var sequenceSerialize = common.serializeCls(messages_proto.SequenceValues);
  var sequenceDeserialize = common.deserializeCls(
      messages_proto.SequenceValues);
  var sequenceBase64Deserialize = common.deserializeCls(
      messages_proto.SequenceValues, true);
  var buffer_val = new Buffer([0x69, 0xb7]);
  var base64_val = 'abc=';
  it('should preserve a buffer', function() {
    var serialized = sequenceSerialize({bytes_field: buffer_val});
    var deserialized = sequenceDeserialize(serialized);
    assert.strictEqual(deserialized.bytes_field.compare(buffer_val), 0);
  });
  it('should accept base64 encoded strings', function() {
    var serialized = sequenceSerialize({bytes_field: base64_val});
    var deserialized = sequenceDeserialize(serialized);
    assert.strictEqual(deserialized.bytes_field.compare(buffer_val), 0);
  });
  it('should output base64 encoded strings with an option set', function() {
    var serialized = sequenceSerialize({bytes_field: base64_val});
    var deserialized = sequenceBase64Deserialize(serialized);
    assert.strictEqual(deserialized.bytes_field, base64_val);
  });
  /* The next two tests are specific tests to verify that issue
   * https://github.com/grpc/grpc/issues/5174 has been fixed. They are skipped
   * because they will not pass until a protobuf.js release has been published
   * with a fix for https://github.com/dcodeIO/protobuf.js/issues/390 */
  it.skip('should serialize a repeated field as packed by default', function() {
    var expected_serialize = new Buffer([0x12, 0x01, 0x01, 0x0a]);
    var serialized = sequenceSerialize({repeated_field: [10]});
    assert.strictEqual(expected_serialize.compare(serialized), 0);
  });
  it.skip('should deserialize packed or unpacked repeated', function() {
    var serialized = new Buffer([0x12, 0x01, 0x01, 0x0a]);
    assert.doesNotThrow(function() {
      sequenceDeserialize(serialized);
    });
  });
});
