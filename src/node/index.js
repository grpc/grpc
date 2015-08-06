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

var grpc = require('bindings')('grpc');

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
 * Get a function that a client can use to update metadata with authentication
 * information from a Google Auth credential object, which comes from the
 * google-auth-library.
 * @param {Object} credential The credential object to use
 * @return {function(Object, callback)} Metadata updater function
 */
exports.getGoogleAuthDelegate = function getGoogleAuthDelegate(credential) {
  /**
   * Update a metadata object with authentication information.
   * @param {string} authURI The uri to authenticate to
   * @param {Object} metadata Metadata object
   * @param {function(Error, Object)} callback
   */
  return function updateMetadata(authURI, metadata, callback) {
    metadata = _.clone(metadata);
    if (metadata.Authorization) {
      metadata.Authorization = _.clone(metadata.Authorization);
    } else {
      metadata.Authorization = [];
    }
    credential.getRequestMetadata(authURI, function(err, header) {
      if (err) {
        callback(err);
        return;
      }
      metadata.Authorization.push(header.Authorization);
      callback(null, metadata);
    });
  };
};

/**
 * @see module:src/server.Server
 */
exports.Server = server.Server;

/**
 * Status name to code number mapping
 */
exports.status = grpc.status;

/**
 * Call error name to code number mapping
 */
exports.callError = grpc.callError;

/**
 * Credentials factories
 */
exports.Credentials = grpc.Credentials;

/**
 * ServerCredentials factories
 */
exports.ServerCredentials = grpc.ServerCredentials;

/**
 * @see module:src/client.makeClientConstructor
 */
exports.makeGenericClientConstructor = client.makeClientConstructor;
