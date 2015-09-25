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

var assert = require('assert');
var fs = require('fs');
var path = require('path');

var grpc = require('..');

describe('client credentials', function() {
  var Client;
  var server;
  var port;
  var client_ssl_creds;
  var client_options = {};
  before(function() {
    var proto = grpc.load(__dirname + '/test_service.proto');
    server = new grpc.Server();
    server.addProtoService(proto.TestService.service, {
      unary: function(call, cb) {
        call.sendMetadata(call.metadata);
        cb(null, {});
      },
      clientStream: function(stream, cb){
        stream.on('data', function(data) {});
        stream.on('end', function() {
          stream.sendMetadata(stream.metadata);
          cb(null, {});
        });
      },
      serverStream: function(stream) {
        stream.sendMetadata(stream.metadata);
        stream.end();
      },
      bidiStream: function(stream) {
        stream.on('data', function(data) {});
        stream.on('end', function() {
          stream.sendMetadata(stream.metadata);
          stream.end();
        });
      }
    });
    var key_path = path.join(__dirname, './data/server1.key');
    var pem_path = path.join(__dirname, './data/server1.pem');
    var key_data = fs.readFileSync(key_path);
    var pem_data = fs.readFileSync(pem_path);
    var creds = grpc.ServerCredentials.createSsl(null,
                                                 [{private_key: key_data,
                                                   cert_chain: pem_data}]);
    //creds = grpc.ServerCredentials.createInsecure();
    port = server.bind('localhost:0', creds);
    server.start();

    Client = proto.TestService;
    var ca_path = path.join(__dirname, '../test/data/ca.pem');
    var ca_data = fs.readFileSync(ca_path);
    client_ssl_creds = grpc.credentials.createSsl(ca_data);
    var host_override = 'foo.test.google.fr';
    client_options['grpc.ssl_target_name_override'] = host_override;
    client_options['grpc.default_authority'] = host_override;
  });
  after(function() {
    server.forceShutdown();
  });
  it.only('Should update metadata with SSL creds', function(done) {
    var metadataUpdater = function(service_url, callback) {
      var metadata = new grpc.Metadata();
      metadata.set('plugin_key', 'plugin_value');
      callback(null, metadata);
    };
    var creds = grpc.credentials.createFromMetadataGenerator(metadataUpdater);
    var combined_creds = grpc.credentials.combineCredentials(client_ssl_creds,
                                                             creds);
    //combined_creds = grpc.credentials.createInsecure();
    var client = new Client('localhost:' + port, combined_creds,
                            client_options);
    var call = client.unary({}, function(err, data) {
      assert.ifError(err);
      console.log('Received response');
    });
    call.on('metadata', function(metadata) {
      assert.deepEqual(metadata.get('plugin_key'), ['plugin_value']);
      done();
    });
  });
});
