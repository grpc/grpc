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

var _ = require('lodash');

var ProtoBuf = require('protobufjs');

var client = require('./src/client.js');

var server = require('./src/server.js');

var Metadata = require('./src/metadata.js');

var grpc = require('bindings')('grpc_node');

/**
 * Load a gRPC object from an existing ProtoBuf.Reflect object.
 * @param {ProtoBuf.Reflect.Namespace} value The ProtoBuf object to load.
 * @return {Object<string, *>} The resulting gRPC object
 */
exports.loadObject = function loadObject(value) {
  var result = {};
  if (value.className === 'Namespace') {
    _.each(value.children, function(child) {
      result[child.name] = loadObject(child);
    });
    return result;
  } else if (value.className === 'Service') {
    return client.makeProtobufClientConstructor(value);
  } else if (value.className === 'Message' || value.className === 'Enum') {
    return value.build();
  } else {
    return value;
  }
};

var loadObject = exports.loadObject;

/**
 * Load a gRPC object from a .proto file.
 * @param {string} filename The file to load
 * @param {string=} format The file format to expect. Must be either 'proto' or
 *     'json'. Defaults to 'proto'
 * @return {Object<string, *>} The resulting gRPC object
 */
exports.load = function load(filename, format) {
  if (!format) {
    format = 'proto';
  }
  var builder;
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
  return loadObject(builder.ns);
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
