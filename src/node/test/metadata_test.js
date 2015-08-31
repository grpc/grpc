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

var Metadata = require('../src/metadata.js');

var assert = require('assert');

describe('Metadata', function() {
  var metadata;
  beforeEach(function() {
    metadata = new Metadata();
  });
  describe('#set', function() {
    it('Only accepts string values for non "-bin" keys', function() {
      assert.throws(function() {
        metadata.set('key', new Buffer('value'));
      });
      assert.doesNotThrow(function() {
        metadata.set('key', 'value');
      });
    });
    it('Only accepts Buffer values for "-bin" keys', function() {
      assert.throws(function() {
        metadata.set('key-bin', 'value');
      });
      assert.doesNotThrow(function() {
        metadata.set('key-bin', new Buffer('value'));
      });
    });
    it('Rejects invalid keys', function() {
      assert.throws(function() {
        metadata.set('key$', 'value');
      });
      assert.throws(function() {
        metadata.set('', 'value');
      });
    });
    it('Rejects values with non-ASCII characters', function() {
      assert.throws(function() {
        metadata.set('key', 'résumé');
      });
    });
    it('Saves values that can be retrieved', function() {
      metadata.set('key', 'value');
      assert.deepEqual(metadata.get('key'), ['value']);
    });
    it('Overwrites previous values', function() {
      metadata.set('key', 'value1');
      metadata.set('key', 'value2');
      assert.deepEqual(metadata.get('key'), ['value2']);
    });
    it('Normalizes keys', function() {
      metadata.set('Key', 'value1');
      assert.deepEqual(metadata.get('key'), ['value1']);
      metadata.set('KEY', 'value2');
      assert.deepEqual(metadata.get('key'), ['value2']);
    });
  });
  describe('#add', function() {
    it('Only accepts string values for non "-bin" keys', function() {
      assert.throws(function() {
        metadata.add('key', new Buffer('value'));
      });
      assert.doesNotThrow(function() {
        metadata.add('key', 'value');
      });
    });
    it('Only accepts Buffer values for "-bin" keys', function() {
      assert.throws(function() {
        metadata.add('key-bin', 'value');
      });
      assert.doesNotThrow(function() {
        metadata.add('key-bin', new Buffer('value'));
      });
    });
    it('Rejects invalid keys', function() {
      assert.throws(function() {
        metadata.add('key$', 'value');
      });
      assert.throws(function() {
        metadata.add('', 'value');
      });
    });
    it('Saves values that can be retrieved', function() {
      metadata.add('key', 'value');
      assert.deepEqual(metadata.get('key'), ['value']);
    });
    it('Combines with previous values', function() {
      metadata.add('key', 'value1');
      metadata.add('key', 'value2');
      assert.deepEqual(metadata.get('key'), ['value1', 'value2']);
    });
    it('Normalizes keys', function() {
      metadata.add('Key', 'value1');
      assert.deepEqual(metadata.get('key'), ['value1']);
      metadata.add('KEY', 'value2');
      assert.deepEqual(metadata.get('key'), ['value1', 'value2']);
    });
  });
  describe('#remove', function() {
    it('clears values from a key', function() {
      metadata.add('key', 'value');
      metadata.remove('key');
      assert.deepEqual(metadata.get('key'), []);
    });
    it('Normalizes keys', function() {
      metadata.add('key', 'value');
      metadata.remove('KEY');
      assert.deepEqual(metadata.get('key'), []);
    });
  });
  describe('#get', function() {
    beforeEach(function() {
      metadata.add('key', 'value1');
      metadata.add('key', 'value2');
      metadata.add('key-bin', new Buffer('value'));
    });
    it('gets all values associated with a key', function() {
      assert.deepEqual(metadata.get('key'), ['value1', 'value2']);
    });
    it('Normalizes keys', function() {
      assert.deepEqual(metadata.get('KEY'), ['value1', 'value2']);
    });
    it('returns an empty list for non-existent keys', function() {
      assert.deepEqual(metadata.get('non-existent-key'), []);
    });
    it('returns Buffers for "-bin" keys', function() {
      assert(metadata.get('key-bin')[0] instanceof Buffer);
    });
  });
  describe('#getMap', function() {
    it('gets a map of keys to values', function() {
      metadata.add('key1', 'value1');
      metadata.add('Key2', 'value2');
      metadata.add('KEY3', 'value3');
      assert.deepEqual(metadata.getMap(),
                       {key1: 'value1',
                        key2: 'value2',
                        key3: 'value3'});
    });
  });
  describe('#clone', function() {
    it('retains values from the original', function() {
      metadata.add('key', 'value');
      var copy = metadata.clone();
      assert.deepEqual(copy.get('key'), ['value']);
    });
    it('Does not see newly added values', function() {
      metadata.add('key', 'value1');
      var copy = metadata.clone();
      metadata.add('key', 'value2');
      assert.deepEqual(copy.get('key'), ['value1']);
    });
    it('Does not add new values to the original', function() {
      metadata.add('key', 'value1');
      var copy = metadata.clone();
      copy.add('key', 'value2');
      assert.deepEqual(metadata.get('key'), ['value1']);
    });
  });
});
