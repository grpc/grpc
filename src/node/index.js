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

var protobuf_js_5_common = require('./src/protobuf_js_5_common');
var protobuf_js_6_common = require('./src/protobuf_js_6_common');

grpc.setDefaultRootsPem(fs.readFileSync(SSL_ROOTS_PATH, 'ascii'));

/**
 * Load a ProtoBuf.js object as a gRPC object. The options object can provide
 * the following options:
 * - binaryAsBase64: deserialize bytes values as base64 strings instead of
 *   Buffers. Defaults to false
 * - longsAsStrings: deserialize long values as strings instead of objects.
 *   Defaults to true
 * - deprecatedArgumentOrder: Use the beta method argument order for client
 *   methods, with optional arguments after the callback. Defaults to false.
 *   This option is only a temporary stopgap measure to smooth an API breakage.
 *   It is deprecated, and new code should not use it.
 * - protobufjsVersion: Available values are 5, 6, and 'detect'. 5 and 6
 *   respectively indicate that an object from the corresponding version of
 *   ProtoBuf.js is provided in the value argument. If the option is 'detect',
 *   gRPC will guess what the version is based on the structure of the value.
 *   Defaults to 'detect'.
 * @param {Object} value The ProtoBuf.js reflection object to load
 * @param {Object=} options Options to apply to the loaded file
 * @return {Object<string, *>} The resulting gRPC object
 */
exports.loadObject = function loadObject(value, options) {
  options = _.defaults(options, common.defaultGrpcOptions);
  options = _.defaults(options, {'protobufjsVersion': 'detect'});
  var protobufjsVersion;
  if (options.protobufjsVersion === 'detect') {
    if (protobuf_js_6_common.isProbablyProtobufJs6(value)) {
      protobufjsVersion = 6;
    } else if (protobuf_js_5_common.isProbablyProtobufJs5(value)) {
      protobufjsVersion = 5;
    } else {
      var error_message = 'Could not detect ProtoBuf.js version. Please ' +
          'specify the version number with the "protobufjs_version" option';
      throw new Error(error_message);
    }
  } else {
    protobufjsVersion = options.protobufjsVersion;
  }
  switch (protobufjsVersion) {
    case 6: return protobuf_js_6_common.loadObject(value, options);
    case 5:
    return protobuf_js_5_common.loadObject(value, options);
    default:
    throw new Error('Unrecognized protobufjsVersion', protobufjsVersion);
  }
};

var loadObject = exports.loadObject;

/**
 * Load a gRPC object from a .proto file. The options object can provide the
 * following options:
 * - convertFieldsToCamelCase: Load this file with field names in camel case
 *   instead of their original case
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
  options = _.defaults(options, common.defaultGrpcOptions);
  options.protobufjsVersion = 5;
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
