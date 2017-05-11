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
 * Wrap a function to pass null-like values through without calling it. If no
 * function is given, just uses the identity.
 * @private
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

/**
 * Default options for loading proto files into gRPC
 */
exports.defaultGrpcOptions = {
  convertFieldsToCamelCase: false,
  binaryAsBase64: false,
  longsAsStrings: true,
  enumsAsStrings: true,
  deprecatedArgumentOrder: false
};

// JSDoc definitions that are used in multiple other modules

/**
 * The EventEmitter class in the event standard module
 * @external EventEmitter
 * @see https://nodejs.org/api/events.html#events_class_eventemitter
 */

/**
 * The Readable class in the stream standard module
 * @external Readable
 * @see https://nodejs.org/api/stream.html#stream_readable_streams
 */

/**
 * The Writable class in the stream standard module
 * @external Writable
 * @see https://nodejs.org/api/stream.html#stream_writable_streams
 */

/**
 * The Duplex class in the stream standard module
 * @external Duplex
 * @see https://nodejs.org/api/stream.html#stream_class_stream_duplex
 */

/**
 * A serialization function
 * @callback module:src/common~serialize
 * @param {*} value The value to serialize
 * @return {Buffer} The value serialized as a byte sequence
 */

/**
 * A deserialization function
 * @callback module:src/common~deserialize
 * @param {Buffer} data The byte sequence to deserialize
 * @return {*} The data deserialized as a value
 */

/**
 * An object that completely defines a service method signature.
 * @typedef {Object} module:src/common~MethodDefinition
 * @property {string} path The method's URL path
 * @property {boolean} requestStream Indicates whether the method accepts
 *     a stream of requests
 * @property {boolean} responseStream Indicates whether the method returns
 *     a stream of responses
 * @property {module:src/common~serialize} requestSerialize Serialization
 *     function for request values
 * @property {module:src/common~serialize} responseSerialize Serialization
 *     function for response values
 * @property {module:src/common~deserialize} requestDeserialize Deserialization
 *     function for request data
 * @property {module:src/common~deserialize} responseDeserialize Deserialization
 *     function for repsonse data
 */

/**
 * An object that completely defines a service.
 * @typedef {Object.<string, module:src/common~MethodDefinition>}
 *     module:src/common~ServiceDefinition
 */
