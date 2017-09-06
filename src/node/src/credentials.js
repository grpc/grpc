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

/**
 * Credentials module
 *
 * This module contains factory methods for two different credential types:
 * CallCredentials and ChannelCredentials. ChannelCredentials are things like
 * SSL credentials that can be used to secure a connection, and are used to
 * construct a Client object. CallCredentials genrally modify metadata, so they
 * can be attached to an individual method call.
 *
 * CallCredentials can be composed with other CallCredentials to create
 * CallCredentials. ChannelCredentials can be composed with CallCredentials
 * to create ChannelCredentials. No combined credential can have more than
 * one ChannelCredentials.
 *
 * For example, to create a client secured with SSL that uses Google
 * default application credentials to authenticate:
 *
 * @example
 * var channel_creds = credentials.createSsl(root_certs);
 * (new GoogleAuth()).getApplicationDefault(function(err, credential) {
 *   var call_creds = credentials.createFromGoogleCredential(credential);
 *   var combined_creds = credentials.combineChannelCredentials(
 *       channel_creds, call_creds);
 *   var client = new Client(address, combined_creds);
 * });
 *
 * @namespace grpc.credentials
 */

'use strict';

var grpc = require('./grpc_extension');

/**
 * This cannot be constructed directly. Instead, instances of this class should
 * be created using the factory functions in {@link grpc.credentials}
 * @constructor grpc.credentials~CallCredentials
 */
var CallCredentials = grpc.CallCredentials;

/**
 * This cannot be constructed directly. Instead, instances of this class should
 * be created using the factory functions in {@link grpc.credentials}
 * @constructor grpc.credentials~ChannelCredentials
 */
var ChannelCredentials = grpc.ChannelCredentials;

var Metadata = require('./metadata.js');

var common = require('./common.js');

var constants = require('./constants');

var _ = require('lodash');

/**
 * @external GoogleCredential
 * @see https://github.com/google/google-auth-library-nodejs
 */

/**
 * Create an SSL Credentials object. If using a client-side certificate, both
 * the second and third arguments must be passed.
 * @memberof grpc.credentials
 * @alias grpc.credentials.createSsl
 * @kind function
 * @param {Buffer=} root_certs The root certificate data
 * @param {Buffer=} private_key The client certificate private key, if
 *     applicable
 * @param {Buffer=} cert_chain The client certificate cert chain, if applicable
 * @return {grpc.credentials.ChannelCredentials} The SSL Credentials object
 */
exports.createSsl = ChannelCredentials.createSsl;

/**
 * @callback grpc.credentials~metadataCallback
 * @param {Error} error The error, if getting metadata failed
 * @param {grpc.Metadata} metadata The metadata
 */

/**
 * @callback grpc.credentials~generateMetadata
 * @param {Object} params Parameters that can modify metadata generation
 * @param {string} params.service_url The URL of the service that the call is
 *     going to
 * @param {grpc.credentials~metadataCallback} callback
 */

/**
 * Create a gRPC credentials object from a metadata generation function. This
 * function gets the service URL and a callback as parameters. The error
 * passed to the callback can optionally have a 'code' value attached to it,
 * which corresponds to a status code that this library uses.
 * @memberof grpc.credentials
 * @alias grpc.credentials.createFromMetadataGenerator
 * @param {grpc.credentials~generateMetadata} metadata_generator The function
 *     that generates metadata
 * @return {grpc.credentials.CallCredentials} The credentials object
 */
exports.createFromMetadataGenerator = function(metadata_generator) {
  return CallCredentials.createFromPlugin(function(service_url, cb_data,
                                                   callback) {
    metadata_generator({service_url: service_url}, function(error, metadata) {
      var code = constants.status.OK;
      var message = '';
      if (error) {
        message = error.message;
        if (error.hasOwnProperty('code') && _.isFinite(error.code)) {
          code = error.code;
        } else {
          code = constants.status.UNAUTHENTICATED;
        }
        if (!metadata) {
          metadata = new Metadata();
        }
      }
      callback(code, message, metadata._getCoreRepresentation(), cb_data);
    });
  });
};

/**
 * Create a gRPC credential from a Google credential object.
 * @memberof grpc.credentials
 * @alias grpc.credentials.createFromGoogleCredential
 * @param {external:GoogleCredential} google_credential The Google credential
 *     object to use
 * @return {grpc.credentials.CallCredentials} The resulting credentials object
 */
exports.createFromGoogleCredential = function(google_credential) {
  return exports.createFromMetadataGenerator(function(auth_context, callback) {
    var service_url = auth_context.service_url;
    google_credential.getRequestMetadata(service_url, function(err, header) {
      if (err) {
        common.log(constants.logVerbosity.INFO, 'Auth error:' + err);
        callback(err);
        return;
      }
      var metadata = new Metadata();
      metadata.add('authorization', header.Authorization);
      callback(null, metadata);
    });
  });
};

/**
 * Combine a ChannelCredentials with any number of CallCredentials into a single
 * ChannelCredentials object.
 * @memberof grpc.credentials
 * @alias grpc.credentials.combineChannelCredentials
 * @param {ChannelCredentials} channel_credential The ChannelCredentials to
 *     start with
 * @param {...CallCredentials} credentials The CallCredentials to compose
 * @return ChannelCredentials A credentials object that combines all of the
 *     input credentials
 */
exports.combineChannelCredentials = function(channel_credential) {
  var current = channel_credential;
  for (var i = 1; i < arguments.length; i++) {
    current = current.compose(arguments[i]);
  }
  return current;
};

/**
 * Combine any number of CallCredentials into a single CallCredentials object
 * @memberof grpc.credentials
 * @alias grpc.credentials.combineCallCredentials
 * @param {...CallCredentials} credentials the CallCredentials to compose
 * @return CallCredentials A credentials object that combines all of the input
 *     credentials
 */
exports.combineCallCredentials = function() {
  var current = arguments[0];
  for (var i = 1; i < arguments.length; i++) {
    current = current.compose(arguments[i]);
  }
  return current;
};

/**
 * Create an insecure credentials object. This is used to create a channel that
 * does not use SSL. This cannot be composed with anything.
 * @memberof grpc.credentials
 * @alias grpc.credentials.createInsecure
 * @kind function
 * @return {ChannelCredentials} The insecure credentials object
 */
exports.createInsecure = ChannelCredentials.createInsecure;
