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
 * @private
 */
exports.logger = console;

/**
 * The current logging verbosity. 0 corresponds to logging everything
 * @private
 */
exports.logVerbosity = 0;

/**
 * Log a message if the severity is at least as high as the current verbosity
 * @private
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
 * @alias grpc~defaultLoadOptions
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
 * Represents the status of a completed request. If `code` is
 * {@link grpc.status}.OK, then the request has completed successfully.
 * Otherwise, the request has failed, `details` will contain a description of
 * the error. Either way, `metadata` contains the trailing response metadata
 * sent by the server when it finishes processing the call.
 * @typedef {object} grpc~StatusObject
 * @property {number} code The error code, a key of {@link grpc.status}
 * @property {string} details Human-readable description of the status
 * @property {grpc.Metadata} metadata Trailing metadata sent with the status,
 *     if applicable
 */

/**
 * Describes how a request has failed. The member `message` will be the same as
 * `details` in {@link grpc~StatusObject}, and `code` and `metadata` are the
 * same as in that object.
 * @typedef {Error} grpc~ServiceError
 * @property {number} code The error code, a key of {@link grpc.status} that is
 *     not `grpc.status.OK`
 * @property {grpc.Metadata} metadata Trailing metadata sent with the status,
 *     if applicable
 */

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
 * @callback grpc~serialize
 * @param {*} value The value to serialize
 * @return {Buffer} The value serialized as a byte sequence
 */

/**
 * A deserialization function
 * @callback grpc~deserialize
 * @param {Buffer} data The byte sequence to deserialize
 * @return {*} The data deserialized as a value
 */

/**
 * The deadline of an operation. If it is a date, the deadline is reached at
 * the date and time specified. If it is a finite number, it is treated as
 * a number of milliseconds since the Unix Epoch. If it is Infinity, the
 * deadline will never be reached. If it is -Infinity, the deadline has already
 * passed.
 * @typedef {(number|date)} grpc~Deadline
 */

/**
 * An object that completely defines a service method signature.
 * @typedef {Object} grpc~MethodDefinition
 * @property {string} path The method's URL path
 * @property {boolean} requestStream Indicates whether the method accepts
 *     a stream of requests
 * @property {boolean} responseStream Indicates whether the method returns
 *     a stream of responses
 * @property {grpc~serialize} requestSerialize Serialization
 *     function for request values
 * @property {grpc~serialize} responseSerialize Serialization
 *     function for response values
 * @property {grpc~deserialize} requestDeserialize Deserialization
 *     function for request data
 * @property {grpc~deserialize} responseDeserialize Deserialization
 *     function for repsonse data
 */

/**
 * An object that completely defines a service.
 * @typedef {Object.<string, grpc~MethodDefinition>} grpc~ServiceDefinition
 */
