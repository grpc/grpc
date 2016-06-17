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
  return _.object(_.map(service.children, function(method) {
    return [_.camelCase(method.name), {
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
    }];
  }));
};
