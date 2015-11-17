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

/**
 * Metadata module
 *
 * This module defines the Metadata class, which represents header and trailer
 * metadata for gRPC calls. Here is an example of how to use it:
 *
 * var metadata = new metadata_module.Metadata();
 * metadata.set('key1', 'value1');
 * metadata.add('key1', 'value2');
 * metadata.get('key1') // returns ['value1', 'value2']
 *
 * @module
 */

'use strict';

var _ = require('lodash');

/**
 * Class for storing metadata. Keys are normalized to lowercase ASCII.
 * @constructor
 */
function Metadata() {
  this._internal_repr = {};
}

function normalizeKey(key) {
  if (!(/^[A-Za-z\d_-]+$/.test(key))) {
    throw new Error('Metadata keys must be nonempty strings containing only ' +
        'alphanumeric characters and hyphens');
  }
  return key.toLowerCase();
}

function validate(key, value) {
  if (_.endsWith(key, '-bin')) {
    if (!(value instanceof Buffer)) {
      throw new Error('keys that end with \'-bin\' must have Buffer values');
    }
  } else {
    if (!_.isString(value)) {
      throw new Error(
          'keys that don\'t end with \'-bin\' must have String values');
    }
    if (!(/^[\x20-\x7E]*$/.test(value))) {
      throw new Error('Metadata string values can only contain printable ' +
          'ASCII characters and space');
    }
  }
}

/**
 * Sets the given value for the given key, replacing any other values associated
 * with that key. Normalizes the key.
 * @param {String} key The key to set
 * @param {String|Buffer} value The value to set. Must be a buffer if and only
 *     if the normalized key ends with '-bin'
 */
Metadata.prototype.set = function(key, value) {
  key = normalizeKey(key);
  validate(key, value);
  this._internal_repr[key] = [value];
};

/**
 * Adds the given value for the given key. Normalizes the key.
 * @param {String} key The key to add to.
 * @param {String|Buffer} value The value to add. Must be a buffer if and only
 *     if the normalized key ends with '-bin'
 */
Metadata.prototype.add = function(key, value) {
  key = normalizeKey(key);
  validate(key, value);
  if (!this._internal_repr[key]) {
    this._internal_repr[key] = [];
  }
  this._internal_repr[key].push(value);
};

/**
 * Remove the given key and any associated values. Normalizes the key.
 * @param {String} key The key to remove
 */
Metadata.prototype.remove = function(key) {
  key = normalizeKey(key);
  if (Object.prototype.hasOwnProperty.call(this._internal_repr, key)) {
    delete this._internal_repr[key];
  }
};

/**
 * Gets a list of all values associated with the key. Normalizes the key.
 * @param {String} key The key to get
 * @return {Array.<String|Buffer>} The values associated with that key
 */
Metadata.prototype.get = function(key) {
  key = normalizeKey(key);
  if (Object.prototype.hasOwnProperty.call(this._internal_repr, key)) {
    return this._internal_repr[key];
  } else {
    return [];
  }
};

/**
 * Get a map of each key to a single associated value. This reflects the most
 * common way that people will want to see metadata.
 * @return {Object.<String,String|Buffer>} A key/value mapping of the metadata
 */
Metadata.prototype.getMap = function() {
  var result = {};
  _.forOwn(this._internal_repr, function(values, key) {
    if(values.length > 0) {
      result[key] = values[0];
    }
  });
  return result;
};

/**
 * Clone the metadata object.
 * @return {Metadata} The new cloned object
 */
Metadata.prototype.clone = function() {
  var copy = new Metadata();
  _.forOwn(this._internal_repr, function(value, key) {
    copy._internal_repr[key] = _.clone(value);
  });
  return copy;
};

/**
 * Gets the metadata in the format used by interal code. Intended for internal
 * use only. API stability is not guaranteed.
 * @private
 * @return {Object.<String, Array.<String|Buffer>>} The metadata
 */
Metadata.prototype._getCoreRepresentation = function() {
  return this._internal_repr;
};

/**
 * Creates a Metadata object from a metadata map in the internal format.
 * Intended for internal use only. API stability is not guaranteed.
 * @private
 * @param {Object.<String, Array.<String|Buffer>>} The metadata
 * @return {Metadata} The new Metadata object
 */
Metadata._fromCoreRepresentation = function(metadata) {
  var newMetadata = new Metadata();
  if (metadata) {
    _.forOwn(metadata, function(value, key) {
      newMetadata._internal_repr[key] = _.clone(value);
    });
  }
  return newMetadata;
};

module.exports = Metadata;
