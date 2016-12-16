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
 * Build the options object to pass to ProtoBuf.Message.toObject
 * @param {bool=} enumsAsStrings Deserialize enum fields as key strings
 *     instead of numbers. Defaults to true
 * @param {bool=} longsAsStrings Deserialize long values as strings instead of
 *     objects. Defaults to true
 * @param {bool=} binaryAsBase64 Deserialize bytes values as base64 strings
 *     instead of Buffers. Defaults to false
 * @return {Object} The options to pass to ProtoBuf.Message.toObject
 */
exports.buildAsJSONOptions = function buildAsJSONOptions(enumsAsStrings,
                                                         longsAsStrings,
                                                         binaryAsBase64) {
  binaryAsBase64 = !!binaryAsBase64;
  if (longsAsStrings === undefined || longsAsStrings === null) {
    longsAsStrings = true;
  }
  if (enumsAsStrings === undefined || enumsAsStrings === null) {
    enumsAsStrings = true;
  }

  // Convert to a native object with binary fields as Buffers (first argument)
  // and longs as strings (second argument)
  var settings = {defaults: true};
  if (longsAsStrings) {
    settings.longs = String;
  }
  if (enumsAsStrings) {
    settings.enums = String;
  }
  if (binaryAsBase64) {
    settings.bytes = String;
  } else {
    settings.bytes = Buffer;
  }
  return settings;
};

/**
 * Get a function that deserializes a specific type of protobuf.
 * @param {function()} cls The ProtoBuf.Type of the message type to deserialize
 * @param {bool=} enumsAsStrings Deserialize enum fields as key strings
 *     instead of numbers. Defaults to true
 * @param {bool=} longsAsStrings Deserialize long values as strings instead of
 *     objects. Defaults to true
 * @param {bool=} binaryAsBase64 Deserialize bytes values as base64 strings
 *     instead of Buffers. Defaults to false
 * @return {function(Buffer):cls} The deserialization function
 */
exports.deserializeCls = function deserializeCls(cls,
  enumsAsStrings, longsAsStrings, binaryAsBase64) {
  var settings = exports.buildAsJSONOptions(enumsAsStrings,
    longsAsStrings,
    binaryAsBase64);

  /**
   * Deserialize a buffer to a message object
   * @param {Buffer} arg_buf The buffer to deserialize
   * @return {cls} The resulting object
   */
  return function deserialize(arg_buf) {
    return cls.decode(arg_buf).toObject(settings);
  };
};

var deserializeCls = exports.deserializeCls;

/**
 * Get a function that serializes objects to a buffer by protobuf class.
 * @param {function()} Cls The ProtoBuf.Type of the message to serialize
 * @return {function(Cls):Buffer} The serialization function
 */
exports.serializeCls = function serializeCls(Cls) {
  /**
   * Serialize an object to a Buffer
   * @param {Object} arg The object to serialize
   * @return {Buffer} The serialized object
   */
  return function serialize(arg) {
    return Cls.encode(arg).finish();
  };
};

var serializeCls = exports.serializeCls;

/**
 * Get the fully qualified (dotted) name of a ProtoBuf.ReflectionObject value.
 * @param {ProtoBuf.ReflectionObject} value The value to get the name of
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
 * @param {ProtoBuf.Service} service The service to get attributes for
 * @param {Object=} options Options to apply to these attributes
 * @return {Object} The attributes map
 */
exports.getProtobufServiceAttrs = function getProtobufServiceAttrs(service,
                                                                   options) {
  var prefix = '/' + fullyQualifiedName(service) + '/';
  var enumsAsStrings, longsAsStrings, binaryAsBase64;
  if (options) {
    enumsAsStrings = options.enumsAsStrings;
    longsAsStrings = options.longsAsStrings;
    binaryAsBase64 = options.binaryAsBase64;
  }

  if (!service.resolved && service.resolveAll) {
    service.resolveAll();
  }

  /* This slightly awkward construction is used to make sure we only use
     lodash@3.10.1-compatible functions. A previous version used
     _.fromPairs, which would be cleaner, but was introduced in lodash
     version 4 */
  return _.zipObject(_.map(service.methods, function(method) {
    return _.camelCase(method.name);
  }), _.map(service.methods, function(method) {
    return {
      path: prefix + method.name,
      requestStream: !!method.requestStream,
      responseStream: !!method.responseStream,
      requestType: method.resolvedRequestType,
      responseType: method.resolvedResponseType,
      requestSerialize: serializeCls(method.resolvedRequestType),
      requestDeserialize: deserializeCls(method.resolvedRequestType,
        enumsAsStrings, longsAsStrings, binaryAsBase64),
      responseSerialize: serializeCls(method.resolvedResponseType),
      responseDeserialize: deserializeCls(method.resolvedResponseType,
        enumsAsStrings, longsAsStrings, binaryAsBase64),
    };
  }));
};

/**
 * Build a grpc-compatible Message prototype.
 * @param {ProtoBuf.Type} type The type build into a Message prototype.
 * @param {Object} options The deserialization options.
 * @return {Object} The message prototype.
 */
exports.makeMessageConstructor = function(type, options) {
  var settings = exports.buildAsJSONOptions(
    options.enumsAsStrings,
    options.longsAsStrings,
    options.binaryAsBase64);

  /**
   * Create a ProtoBuf.Message with the given properties.
   * @constructor
   * @param {Object} properties Properties to set.
   */
  function Message(properties) {
    // Validate and format the properties as per our options.
    var jsonObj = type.create(properties).toObject(settings);
    // Apply the fields to the constructed object.
    Object.assign(this, jsonObj);
  }

  // Copy old static functions
  Message.encode = function(properties) {
    return type.encode(properties).finish();
  };
  Message.decode = Message.decode64 = Message.decodeHex = function(data) {
    return type.decode(data).toObject(settings);
  };
  Message.decodeJSON = function(jsonData) {
    return type.create(JSON.parse(jsonData)).toObject(settings);
  };
  Message.decodeDelimited = function(data) {
    return type.decodeDelimited(data).toObject(settings);
  };

  // Build compatible prototype
  Message.prototype.encode = function() {
    return Message.encode(this);
  };
  Message.prototype.encodeHex = function() {
    return this.encode().toString('hex');
  };
  Message.prototype.encodeJSON = function() {
    var jsonObj = type.create(this).toObject(settings);
    return JSON.stringify(jsonObj);
  };
  Message.prototype.encodeDelimited = function() {
    return type.encodeDelimited(this).finish();
  };
  Message.prototype.toRaw = function() {
    return type.create(this).toObject(settings);
  };
  Message.prototype.toBuffer = function() {
    return this.encode();
  };

  return Message;
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
