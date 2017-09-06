/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
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
