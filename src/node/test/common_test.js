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
var _ = require('lodash');

var common = require('../src/common');
var protobuf_js_6_common = require('../src/protobuf_js_6_common');

var serializeCls = protobuf_js_6_common.serializeCls;
var deserializeCls = protobuf_js_6_common.deserializeCls;

var ProtoBuf = require('protobufjs');

var messages_proto = new ProtoBuf.Root();
messages_proto = messages_proto.loadSync(
    __dirname + '/test_messages.proto', {keepCase: true}).resolveAll();

var default_options = common.defaultGrpcOptions;

describe('Proto message long int serialize and deserialize', function() {
  var longSerialize = serializeCls(messages_proto.LongValues);
  var longDeserialize = deserializeCls(messages_proto.LongValues,
                                       default_options);
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
    var num_options = _.defaults({longsAsStrings: false}, default_options);
    var longNumDeserialize = deserializeCls(messages_proto.LongValues,
                                            num_options);
    var serialized = longSerialize({int_64: pos_value});
    assert.strictEqual(typeof longDeserialize(serialized).int_64, 'string');
    /* With the longsAsStrings option disabled, long values are represented as
     * objects with 3 keys: low, high, and unsigned */
    assert.strictEqual(typeof longNumDeserialize(serialized).int_64, 'object');
  });
});
describe('Proto message bytes serialize and deserialize', function() {
  var sequenceSerialize = serializeCls(messages_proto.SequenceValues);
  var sequenceDeserialize = deserializeCls(
      messages_proto.SequenceValues, default_options);
  var b64_options = _.defaults({binaryAsBase64: true}, default_options);
  var sequenceBase64Deserialize = deserializeCls(
      messages_proto.SequenceValues, b64_options);
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
  it('should serialize a repeated field as packed by default', function() {
    var expected_serialize = new Buffer([0x12, 0x01, 0x0a]);
    var serialized = sequenceSerialize({repeated_field: [10]});
    assert.strictEqual(expected_serialize.compare(serialized), 0);
  });
  it('should deserialize packed or unpacked repeated', function() {
    var expectedDeserialize = {
      bytes_field: new Buffer(''),
      repeated_field: [10]
    };
    var packedSerialized = new Buffer([0x12, 0x01, 0x0a]);
    var unpackedSerialized = new Buffer([0x10, 0x0a]);
    var packedDeserialized;
    var unpackedDeserialized;
    assert.doesNotThrow(function() {
      packedDeserialized = sequenceDeserialize(packedSerialized);
    });
    assert.doesNotThrow(function() {
      unpackedDeserialized = sequenceDeserialize(unpackedSerialized);
    });
    assert.deepEqual(packedDeserialized, expectedDeserialize);
    assert.deepEqual(unpackedDeserialized, expectedDeserialize);
  });
});
describe('Proto message oneof serialize and deserialize', function() {
  var oneofSerialize = serializeCls(messages_proto.OneOfValues);
  var oneofDeserialize = deserializeCls(
      messages_proto.OneOfValues, default_options);
  it('Should have idempotent round trips', function() {
    var test_message = {oneof_choice: 'int_choice', int_choice: 5};
    var serialized1 = oneofSerialize(test_message);
    var deserialized1 = oneofDeserialize(serialized1);
    assert.equal(deserialized1.int_choice, 5);
    var serialized2 = oneofSerialize(deserialized1);
    var deserialized2 = oneofDeserialize(serialized2);
    assert.deepEqual(deserialized1, deserialized2);
  });
  it('Should emit a property indicating which field was chosen', function() {
    var test_message1 = {oneof_choice: 'int_choice', int_choice: 5};
    var serialized1 = oneofSerialize(test_message1);
    var deserialized1 = oneofDeserialize(serialized1);
    assert.equal(deserialized1.oneof_choice, 'int_choice');
    var test_message2 = {oneof_choice: 'string_choice', string_choice: 'abc'};
    var serialized2 = oneofSerialize(test_message2);
    var deserialized2 = oneofDeserialize(serialized2);
    assert.equal(deserialized2.oneof_choice, 'string_choice');
  });
});
describe('Proto message enum serialize and deserialize', function() {
  var enumSerialize = serializeCls(messages_proto.EnumValues);
  var enumDeserialize = deserializeCls(
      messages_proto.EnumValues, default_options);
  var enumIntOptions = _.defaults({enumsAsStrings: false}, default_options);
  var enumIntDeserialize = deserializeCls(
      messages_proto.EnumValues, enumIntOptions);
  it('Should accept both names and numbers', function() {
    var nameSerialized = enumSerialize({enum_value: 'ONE'});
    var numberSerialized = enumSerialize({enum_value: 1});
    assert.strictEqual(messages_proto.TestEnum.ONE, 1);
    assert.deepEqual(enumDeserialize(nameSerialized),
                     enumDeserialize(numberSerialized));
  });
  it('Should deserialize as a string the enumsAsStrings option', function() {
    var serialized = enumSerialize({enum_value: 'TWO'});
    var nameDeserialized = enumDeserialize(serialized);
    var numberDeserialized = enumIntDeserialize(serialized);
    assert.deepEqual(nameDeserialized, {enum_value: 'TWO'});
    assert.deepEqual(numberDeserialized, {enum_value: 2});
  });
});
