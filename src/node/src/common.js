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

/**
 * This module contains functions that are common to client and server
 * code. None of them should be used directly by gRPC users.
 * @module
 */

'use strict';

var _ = require('lodash');

/**
 * Get a function that deserializes a specific type of protobuf.
 * @param {function()} cls The constructor of the message type to deserialize
 * @param {bool=} binaryAsBase64 Deserialize bytes fields as base64 strings
 *     instead of Buffers. Defaults to false
 * @param {bool=} longsAsStrings Deserialize long values as strings instead of
 *     objects. Defaults to true
 * @return {function(Buffer):cls} The deserialization function
 */
exports.deserializeCls = function deserializeCls(cls, binaryAsBase64,
                                                 longsAsStrings) {
  if (binaryAsBase64 === undefined || binaryAsBase64 === null) {
    binaryAsBase64 = false;
  }
  if (longsAsStrings === undefined || longsAsStrings === null) {
    longsAsStrings = true;
  }
  /**
   * Deserialize a buffer to a message object
   * @param {Buffer} arg_buf The buffer to deserialize
   * @return {cls} The resulting object
   */
  return function deserialize(arg_buf) {
    // Convert to a native object with binary fields as Buffers (first argument)
    // and longs as strings (second argument)
    return cls.decode(arg_buf).toRaw(binaryAsBase64, longsAsStrings);
  };
};

var deserializeCls = exports.deserializeCls;

/**
 * Get a function that serializes objects to a buffer by protobuf class.
 * @param {function()} Cls The constructor of the message type to serialize
 * @return {function(Cls):Buffer} The serialization function
 */
exports.serializeCls = function serializeCls(Cls) {
  /**
   * Serialize an object to a Buffer
   * @param {Object} arg The object to serialize
   * @return {Buffer} The serialized object
   */
  return function serialize(arg) {
    return new Buffer(new Cls(arg).encode().toBuffer());
  };
};

var serializeCls = exports.serializeCls;

/**
 * Get the fully qualified (dotted) name of a ProtoBuf.Reflect value.
 * @param {ProtoBuf.Reflect.Namespace} value The value to get the name of
 * @return {string} The fully qualified name of the value
 */
exports.fullyQualifiedName = function fullyQualifiedName(value) {
  if (value === null || value === undefined) {
    return '';
  }
  var name = value.name;
  var parent_name = fullyQualifiedName(value.parent);
  if (parent_name !== '') {
    name = parent_name + '.' + name;
  }
  return name;
};

var fullyQualifiedName = exports.fullyQualifiedName;

/**
 * Wrap a function to pass null-like values through without calling it. If no
 * function is given, just uses the identity;
 * @param {?function} func The function to wrap
 * @return {function} The wrapped function
 */
exports.wrapIgnoreNull = function wrapIgnoreNull(func) {
  if (!func) {
    return _.identity;
  }
  return function(arg) {
    if (arg === null || arg === undefined) {
      return null;
    }
    return func(arg);
  };
};

/**
 * Return a map from method names to method attributes for the service.
 * @param {ProtoBuf.Reflect.Service} service The service to get attributes for
 * @param {Object=} options Options to apply to these attributes
 * @return {Object} The attributes map
 */
exports.getProtobufServiceAttrs = function getProtobufServiceAttrs(service,
                                                                   options) {
  var prefix = '/' + fullyQualifiedName(service) + '/';
  var binaryAsBase64, longsAsStrings;
  if (options) {
    binaryAsBase64 = options.binaryAsBase64;
    longsAsStrings = options.longsAsStrings;
  }
  /* This slightly awkward construction is used to make sure we only use
     lodash@3.10.1-compatible functions. A previous version used
     _.fromPairs, which would be cleaner, but was introduced in lodash
     version 4 */
  return _.zipObject(_.map(service.children, function(method) {
    return _.camelCase(method.name);
  }), _.map(service.children, function(method) {
    return {
      path: prefix + method.name,
      requestStream: method.requestStream,
      responseStream: method.responseStream,
      requestType: method.resolvedRequestType,
      responseType: method.resolvedResponseType,
      requestSerialize: serializeCls(method.resolvedRequestType.build()),
      requestDeserialize: deserializeCls(method.resolvedRequestType.build(),
                                         binaryAsBase64, longsAsStrings),
      responseSerialize: serializeCls(method.resolvedResponseType.build()),
      responseDeserialize: deserializeCls(method.resolvedResponseType.build(),
                                          binaryAsBase64, longsAsStrings)
    };
  }));
};

/**
 * The logger object for the gRPC module. Defaults to console.
 */
exports.logger = console;

/**
 * The current logging verbosity. 0 corresponds to logging everything
 */
exports.logVerbosity = 0;

/**
 * Log a message if the severity is at least as high as the current verbosity
 * @param {Number} severity A value of the grpc.logVerbosity map
 * @param {String} message The message to log
 */
exports.log = function log(severity, message) {
  if (severity >= exports.logVerbosity) {
    exports.logger.error(message);
  }
};
