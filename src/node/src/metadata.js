/**
 * @license
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

var _ = require('lodash');

var grpc = require('./grpc_extension');

/**
 * Class for storing metadata. Keys are normalized to lowercase ASCII.
 * @memberof grpc
 * @constructor
 * @example
 * var metadata = new metadata_module.Metadata();
 * metadata.set('key1', 'value1');
 * metadata.add('key1', 'value2');
 * metadata.get('key1') // returns ['value1', 'value2']
 */
function Metadata() {
  this._internal_repr = {};
}

function normalizeKey(key) {
  key = key.toLowerCase();
  if (grpc.metadataKeyIsLegal(key)) {
    return key;
  } else {
    throw new Error('Metadata key"' + key + '" contains illegal characters');
  }
}

function validate(key, value) {
  if (grpc.metadataKeyIsBinary(key)) {
    if (!(value instanceof Buffer)) {
      throw new Error('keys that end with \'-bin\' must have Buffer values');
    }
  } else {
    if (!_.isString(value)) {
      throw new Error(
          'keys that don\'t end with \'-bin\' must have String values');
    }
    if (!grpc.metadataNonbinValueIsLegal(value)) {
      throw new Error('Metadata string value "' + value +
                      '" contains illegal characters');
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
