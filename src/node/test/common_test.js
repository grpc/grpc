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

var messages_proto = ProtoBuf.loadProtoFile(
    __dirname + '/test_messages.proto').build();

describe('Proto message serialize and deserialize', function() {
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
});
