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

var constants = require('./src/constants.js');

grpc.setDefaultRootsPem(fs.readFileSync(SSL_ROOTS_PATH, 'ascii'));

/**
 * @namespace grpc
 */

/**
 * Load a ProtoBuf.js object as a gRPC object.
 * @memberof grpc
 * @alias grpc.loadObject
 * @param {Object} value The ProtoBuf.js reflection object to load
 * @param {Object=} options Options to apply to the loaded file
 * @param {bool=} [options.binaryAsBase64=false] deserialize bytes values as
 *     base64 strings instead of Buffers
 * @param {bool=} [options.longsAsStrings=true] deserialize long values as
 *     strings instead of objects
 * @param {bool=} [options.enumsAsStrings=true] deserialize enum values as
 *     strings instead of numbers. Only works with Protobuf.js 6 values.
 * @param {bool=} [options.deprecatedArgumentOrder=false] use the beta method
 *     argument order for client methods, with optional arguments after the
 *     callback. This option is only a temporary stopgap measure to smooth an
 *     API breakage. It is deprecated, and new code should not use it.
 * @param {(number|string)=} [options.protobufjsVersion='detect'] 5 and 6
 *     respectively indicate that an object from the corresponding version of
 *     Protobuf.js is provided in the value argument. If the option is 'detect',
 *     gRPC wll guess what the version is based on the structure of the value.
 * @return {Object<string, *>} The resulting gRPC object.
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
 * Load a gRPC object from a .proto file.
 * @memberof grpc
 * @alias grpc.load
 * @param {string|{root: string, file: string}} filename The file to load
 * @param {string=} format The file format to expect. Must be either 'proto' or
 *     'json'. Defaults to 'proto'
 * @param {Object=} options Options to apply to the loaded file
 * @param {bool=} [options.convertFieldsToCamelCase=false] Load this file with
 *     field names in camel case instead of their original case
 * @param {bool=} [options.binaryAsBase64=false] deserialize bytes values as
 *     base64 strings instead of Buffers
 * @param {bool=} [options.longsAsStrings=true] deserialize long values as
 *     strings instead of objects
 * @param {bool=} [options.deprecatedArgumentOrder=false] use the beta method
 *     argument order for client methods, with optional arguments after the
 *     callback. This option is only a temporary stopgap measure to smooth an
 *     API breakage. It is deprecated, and new code should not use it.
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
 * @memberof grpc
 * @alias grpc.setLogger
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
 * @memberof grpc
 * @alias grpc.setLogVerbosity
 * @param {Number} verbosity The minimum severity to log
 */
exports.setLogVerbosity = function setLogVerbosity(verbosity) {
  common.logVerbosity = verbosity;
  grpc.setLogVerbosity(verbosity);
};

exports.Server = server.Server;

exports.Metadata = Metadata;

exports.status = constants.status;

exports.propagate = constants.propagate;

exports.callError = constants.callError;

exports.writeFlags = constants.writeFlags;

exports.logVerbosity = constants.logVerbosity;

exports.credentials = require('./src/credentials.js');

/**
 * ServerCredentials factories
 * @constructor ServerCredentials
 * @memberof grpc
 */
exports.ServerCredentials = grpc.ServerCredentials;

/**
 * Create insecure server credentials
 * @name grpc.ServerCredentials.createInsecure
 * @kind function
 * @return grpc.ServerCredentials
 */

/**
 * A private key and certificate pair
 * @typedef {Object} grpc.ServerCredentials~keyCertPair
 * @property {Buffer} privateKey The server's private key
 * @property {Buffer} certChain The server's certificate chain
 */

/**
 * Create SSL server credentials
 * @name grpc.ServerCredentials.createInsecure
 * @kind function
 * @param {?Buffer} rootCerts Root CA certificates for validating client
 *     certificates
 * @param {Array<grpc.ServerCredentials~keyCertPair>} keyCertPairs A list of
 *     private key and certificate chain pairs to be used for authenticating
 *     the server
 * @param {boolean} [checkClientCertificate=false] Indicates that the server
 *     should request and verify the client's certificates
 * @return grpc.ServerCredentials
 */

exports.makeGenericClientConstructor = client.makeClientConstructor;

exports.getClientChannel = client.getClientChannel;

exports.waitForClientReady = client.waitForClientReady;

/**
 * @memberof grpc
 * @alias grpc.closeClient
 * @param {grpc.Client} client_obj The client to close
 */
exports.closeClient = function closeClient(client_obj) {
  client.Client.prototype.close.apply(client_obj);
};

exports.Client = client.Client;
