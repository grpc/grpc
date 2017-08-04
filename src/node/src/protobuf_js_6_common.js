/**
 * @license
 * Copyright 2017 gRPC authors.
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

/**
 * @module
 * @private
 */

'use strict';

var _ = require('lodash');
var client = require('./client');

/**
 * Get a function that deserializes a specific type of protobuf.
 * @param {function()} cls The constructor of the message type to deserialize
 * @param {bool=} binaryAsBase64 Deserialize bytes fields as base64 strings
 *     instead of Buffers. Defaults to false
 * @param {bool=} longsAsStrings Deserialize long values as strings instead of
 *     objects. Defaults to true
 * @return {function(Buffer):cls} The deserialization function
 */
exports.deserializeCls = function deserializeCls(cls, options) {
  var conversion_options = {
    defaults: true,
    bytes: options.binaryAsBase64 ? String : Buffer,
    longs: options.longsAsStrings ? String : null,
    enums: options.enumsAsStrings ? String : null,
    oneofs: true
  };
  /**
   * Deserialize a buffer to a message object
   * @param {Buffer} arg_buf The buffer to deserialize
   * @return {cls} The resulting object
   */
  return function deserialize(arg_buf) {
    return cls.toObject(cls.decode(arg_buf), conversion_options);
  };
};

var deserializeCls = exports.deserializeCls;

/**
 * Get a function that serializes objects to a buffer by protobuf class.
 * @param {function()} Cls The constructor of the message type to serialize
 * @return {function(Cls):Buffer} The serialization function
 */
exports.serializeCls = function serializeCls(cls) {
  /**
   * Serialize an object to a Buffer
   * @param {Object} arg The object to serialize
   * @return {Buffer} The serialized object
   */
  return function serialize(arg) {
    var message = cls.fromObject(arg);
    return cls.encode(message).finish();
  };
};

var serializeCls = exports.serializeCls;

/**
 * Get the fully qualified (dotted) name of a ProtoBuf.Reflect value.
 * @param {ProtoBuf.ReflectionObject} value The value to get the name of
 * @return {string} The fully qualified name of the value
 */
exports.fullyQualifiedName = function fullyQualifiedName(value) {
  if (value === null || value === undefined) {
    return '';
  }
  var name = value.name;
  var parent_fqn = fullyQualifiedName(value.parent);
  if (parent_fqn !== '') {
    name = parent_fqn + '.' + name;
  }
  return name;
};

var fullyQualifiedName = exports.fullyQualifiedName;

/**
 * Return a map from method names to method attributes for the service.
 * @param {ProtoBuf.Service} service The service to get attributes for
 * @param {Object=} options Options to apply to these attributes
 * @return {Object} The attributes map
 */
exports.getProtobufServiceAttrs = function getProtobufServiceAttrs(service,
                                                                   options) {
  var prefix = '/' + fullyQualifiedName(service) + '/';
  service.resolveAll();
  return _.zipObject(_.map(service.methods, function(method) {
    return _.camelCase(method.name);
  }), _.map(service.methods, function(method) {
    return {
      originalName: method.name,
      path: prefix + method.name,
      requestStream: !!method.requestStream,
      responseStream: !!method.responseStream,
      requestType: method.resolvedRequestType,
      responseType: method.resolvedResponseType,
      requestSerialize: serializeCls(method.resolvedRequestType),
      requestDeserialize: deserializeCls(method.resolvedRequestType, options),
      responseSerialize: serializeCls(method.resolvedResponseType),
      responseDeserialize: deserializeCls(method.resolvedResponseType, options)
    };
  }));
};

var getProtobufServiceAttrs = exports.getProtobufServiceAttrs;

exports.loadObject = function loadObject(value, options) {
  var result = {};
  if (!value) {
    return value;
  }
  if (value.hasOwnProperty('methods')) {
    // It's a service object
    var service_attrs = getProtobufServiceAttrs(value, options);
    return client.makeClientConstructor(service_attrs);
  }

  if (value.hasOwnProperty('nested')) {
    // It's a namespace or root object
    _.each(value.nested, function(nested, name) {
      result[name] = loadObject(nested, options);
    });
    return result;
  }

  // Otherwise, it's not something we need to change
  return value;
};

/**
 * The primary purpose of this method is to distinguish between reflection
 * objects from different versions of ProtoBuf.js. This is just a heuristic,
 * checking for properties that are (currently) specific to this version of
 * ProtoBuf.js
 * @param {Object} obj The object to check
 * @return {boolean} Whether the object appears to be a Protobuf.js 6
 *   ReflectionObject
 */
exports.isProbablyProtobufJs6 = function isProbablyProtobufJs6(obj) {
  return (typeof obj.root === 'object') && (typeof obj.resolve === 'function');
};
