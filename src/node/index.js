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

'use strict';

var path = require('path');
var fs = require('fs');

var SSL_ROOTS_PATH = path.resolve(__dirname, '..', '..', 'etc', 'roots.pem');

var _ = require('lodash');

var ProtoBuf = require('protobufjs');

var client = require('./src/client.js');

var server = require('./src/server.js');

var common = require('./src/common.js');

var Metadata = require('./src/metadata.js');

var grpc = require('./src/grpc_extension');

grpc.setDefaultRootsPem(fs.readFileSync(SSL_ROOTS_PATH, 'ascii'));

/**
 * Load a gRPC object from an existing ProtoBuf.Reflect object.
 * @param {ProtoBuf.Reflect.Namespace} value The ProtoBuf object to load.
 * @param {Object=} options Options to apply to the loaded object
 * @return {Object<string, *>} The resulting gRPC object
 */
exports.loadObject = function loadObject(value, options) {
  var result = {};
  if (value.className === 'Namespace') {
    _.each(value.children, function(child) {
      result[child.name] = loadObject(child, options);
    });
    return result;
  } else if (value.className === 'Service') {
    return client.makeProtobufClientConstructor(value, options);
  } else if (value.className === 'Message' || value.className === 'Enum') {
    return value.build();
  } else {
    return value;
  }
};

var loadObject = exports.loadObject;

/**
 * Load a gRPC object from a .proto file. The options object can provide the
 * following options:
 * - convertFieldsToCamelCase: Loads this file with that option on protobuf.js
 *   set as specified. See
 *   https://github.com/dcodeIO/protobuf.js/wiki/Advanced-options for details
 * - binaryAsBase64: deserialize bytes values as base64 strings instead of
 *   Buffers. Defaults to false
 * - longsAsStrings: deserialize long values as strings instead of objects.
 *   Defaults to true
 * - deprecatedArgumentOrder: Use the beta method argument order for client
 *   methods, with optional arguments after the callback. Defaults to false.
 *   This option is only a temporary stopgap measure to smooth an API breakage.
 *   It is deprecated, and new code should not use it.
 * @param {string|{root: string, file: string}} filename The file to load
 * @param {string=} format The file format to expect. Must be either 'proto' or
 *     'json'. Defaults to 'proto'
 * @param {Object=} options Options to apply to the loaded file
 * @return {Object<string, *>} The resulting gRPC object
 */
exports.load = function load(filename, format, options) {
  if (!format) {
    format = 'proto';
  }
  var convertFieldsToCamelCaseOriginal = ProtoBuf.convertFieldsToCamelCase;
  if(options && options.hasOwnProperty('convertFieldsToCamelCase')) {
    ProtoBuf.convertFieldsToCamelCase = options.convertFieldsToCamelCase;
  }
  var builder;
  try {
    switch(format) {
      case 'proto':
      builder = ProtoBuf.loadProtoFile(filename);
      break;
      case 'json':
      builder = ProtoBuf.loadJsonFile(filename);
      break;
      default:
      throw new Error('Unrecognized format "' + format + '"');
    }
  } finally {
    ProtoBuf.convertFieldsToCamelCase = convertFieldsToCamelCaseOriginal;
  }
  return loadObject(builder.ns, options);
};

var log_template = _.template(
    '{severity} {timestamp}\t{file}:{line}]\t{message}',
    {interpolate: /{([\s\S]+?)}/g});

/**
 * Sets the logger function for the gRPC module. For debugging purposes, the C
 * core will log synchronously directly to stdout unless this function is
 * called. Note: the output format here is intended to be informational, and
 * is not guaranteed to stay the same in the future.
 * Logs will be directed to logger.error.
 * @param {Console} logger A Console-like object.
 */
exports.setLogger = function setLogger(logger) {
  common.logger = logger;
  grpc.setDefaultLoggerCallback(function(file, line, severity,
                                         message, timestamp) {
    logger.error(log_template({
      file: path.basename(file),
      line: line,
      severity: severity,
      message: message,
      timestamp: timestamp.toISOString()
    }));
  });
};

/**
 * Sets the logger verbosity for gRPC module logging. The options are members
 * of the grpc.logVerbosity map.
 * @param {Number} verbosity The minimum severity to log
 */
exports.setLogVerbosity = function setLogVerbosity(verbosity) {
  common.logVerbosity = verbosity;
  grpc.setLogVerbosity(verbosity);
};

/**
 * @see module:src/server.Server
 */
exports.Server = server.Server;

/**
 * @see module:src/metadata
 */
exports.Metadata = Metadata;

/**
 * Status name to code number mapping
 */
exports.status = grpc.status;

/**
 * Propagate flag name to number mapping
 */
exports.propagate = grpc.propagate;

/**
 * Call error name to code number mapping
 */
exports.callError = grpc.callError;

/**
 * Write flag name to code number mapping
 */
exports.writeFlags = grpc.writeFlags;

/**
 * Log verbosity setting name to code number mapping
 */
exports.logVerbosity = grpc.logVerbosity;

/**
 * Credentials factories
 */
exports.credentials = require('./src/credentials.js');

/**
 * ServerCredentials factories
 */
exports.ServerCredentials = grpc.ServerCredentials;

/**
 * @see module:src/client.makeClientConstructor
 */
exports.makeGenericClientConstructor = client.makeClientConstructor;

/**
 * @see module:src/client.getClientChannel
 */
exports.getClientChannel = client.getClientChannel;

/**
 * @see module:src/client.waitForClientReady
 */
exports.waitForClientReady = client.waitForClientReady;

exports.closeClient = function closeClient(client_obj) {
  client.getClientChannel(client_obj).close();
};
