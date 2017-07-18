/*
 *
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

var assert = require('assert');
var fs = require('fs');
var path = require('path');

var grpc = require('..');

/**
 * This is used for testing functions with multiple asynchronous calls that
 * can happen in different orders. This should be passed the number of async
 * function invocations that can occur last, and each of those should call this
 * function's return value
 * @param {function()} done The function that should be called when a test is
 *     complete.
 * @param {number} count The number of calls to the resulting function if the
 *     test passes.
 * @return {function()} The function that should be called at the end of each
 *     sequence of asynchronous functions.
 */
function multiDone(done, count) {
  return function() {
    count -= 1;
    if (count <= 0) {
      done();
    }
  };
}

var fakeSuccessfulGoogleCredentials = {
  getRequestMetadata: function(service_url, callback) {
    setTimeout(function() {
      callback(null, {Authorization: 'success'});
    }, 0);
  }
};

var fakeFailingGoogleCredentials = {
  getRequestMetadata: function(service_url, callback) {
    setTimeout(function() {
      // Google credentials currently adds string error codes to auth errors
      var error = new Error('Authentication failure');
      error.code = 'ENOENT';
      callback(error);
    }, 0);
  }
};

var key_data, pem_data, ca_data;

before(function() {
  var key_path = path.join(__dirname, './data/server1.key');
  var pem_path = path.join(__dirname, './data/server1.pem');
  var ca_path = path.join(__dirname, '../test/data/ca.pem');
  key_data = fs.readFileSync(key_path);
  pem_data = fs.readFileSync(pem_path);
  ca_data = fs.readFileSync(ca_path);
});

describe('channel credentials', function() {
  describe('#createSsl', function() {
    it('works with no arguments', function() {
      var creds;
      assert.doesNotThrow(function() {
        creds = grpc.credentials.createSsl();
      });
      assert.notEqual(creds, null);
    });
    it('works with just one Buffer argument', function() {
      var creds;
      assert.doesNotThrow(function() {
        creds = grpc.credentials.createSsl(ca_data);
      });
      assert.notEqual(creds, null);
    });
    it('works with 3 Buffer arguments', function() {
      var creds;
      assert.doesNotThrow(function() {
        creds = grpc.credentials.createSsl(ca_data, key_data, pem_data);
      });
      assert.notEqual(creds, null);
    });
    it('works if the first argument is null', function() {
      var creds;
      assert.doesNotThrow(function() {
        creds = grpc.credentials.createSsl(null, key_data, pem_data);
      });
      assert.notEqual(creds, null);
    });
    it('fails if the first argument is a non-Buffer value', function() {
      assert.throws(function() {
        grpc.credentials.createSsl('test');
      }, TypeError);
    });
    it('fails if the second argument is a non-Buffer value', function() {
      assert.throws(function() {
        grpc.credentials.createSsl(null, 'test', pem_data);
      }, TypeError);
    });
    it('fails if the third argument is a non-Buffer value', function() {
      assert.throws(function() {
        grpc.credentials.createSsl(null, key_data, 'test');
      }, TypeError);
    });
    it('fails if only 1 of the last 2 arguments is provided', function() {
      assert.throws(function() {
        grpc.credentials.createSsl(null, key_data);
      });
      assert.throws(function() {
        grpc.credentials.createSsl(null, null, pem_data);
      });
    });
  });
});

describe('server credentials', function() {
  describe('#createSsl', function() {
    it('accepts a buffer and array as the first 2 arguments', function() {
      var creds;
      assert.doesNotThrow(function() {
        creds = grpc.ServerCredentials.createSsl(ca_data, []);
      });
      assert.notEqual(creds, null);
    });
    it('accepts a boolean as the third argument', function() {
      var creds;
      assert.doesNotThrow(function() {
        creds = grpc.ServerCredentials.createSsl(ca_data, [], true);
      });
      assert.notEqual(creds, null);
    });
    it('accepts an object with two buffers in the second argument', function() {
      var creds;
      assert.doesNotThrow(function() {
        creds = grpc.ServerCredentials.createSsl(null,
                                                 [{private_key: key_data,
                                                   cert_chain: pem_data}]);
      });
      assert.notEqual(creds, null);
    });
    it('accepts multiple objects in the second argument', function() {
      var creds;
      assert.doesNotThrow(function() {
        creds = grpc.ServerCredentials.createSsl(null,
                                                 [{private_key: key_data,
                                                   cert_chain: pem_data},
                                                  {private_key: key_data,
                                                   cert_chain: pem_data}]);
      });
      assert.notEqual(creds, null);
    });
    it('fails if the second argument is not an Array', function() {
      assert.throws(function() {
        grpc.ServerCredentials.createSsl(ca_data, 'test');
      }, TypeError);
    });
    it('fails if the first argument is a non-Buffer value', function() {
      assert.throws(function() {
        grpc.ServerCredentials.createSsl('test', []);
      }, TypeError);
    });
    it('fails if the third argument is a non-boolean value', function() {
      assert.throws(function() {
        grpc.ServerCredentials.createSsl(ca_data, [], 'test');
      }, TypeError);
    });
    it('fails if the array elements are not objects', function() {
      assert.throws(function() {
        grpc.ServerCredentials.createSsl(ca_data, 'test');
      }, TypeError);
    });
    it('fails if the object does not have a Buffer private_key', function() {
      assert.throws(function() {
        grpc.ServerCredentials.createSsl(null,
                                         [{private_key: 'test',
                                           cert_chain: pem_data}]);
      }, TypeError);
    });
    it('fails if the object does not have a Buffer cert_chain', function() {
      assert.throws(function() {
        grpc.ServerCredentials.createSsl(null,
                                         [{private_key: key_data,
                                           cert_chain: 'test'}]);
      }, TypeError);
    });
  });
});

describe('client credentials', function() {
  var Client;
  var server;
  var port;
  var client_ssl_creds;
  var client_options = {};
  before(function() {
    var proto = grpc.load(__dirname + '/test_service.proto');
    server = new grpc.Server();
    server.addService(proto.TestService.service, {
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
    var creds = grpc.ServerCredentials.createSsl(null,
                                                 [{private_key: key_data,
                                                   cert_chain: pem_data}]);
    port = server.bind('localhost:0', creds);
    server.start();

    Client = proto.TestService;
    client_ssl_creds = grpc.credentials.createSsl(ca_data);
    var host_override = 'foo.test.google.fr';
    client_options['grpc.ssl_target_name_override'] = host_override;
    client_options['grpc.default_authority'] = host_override;
  });
  after(function() {
    server.forceShutdown();
  });
  it('Should accept SSL creds for a client', function(done) {
    var client = new Client('localhost:' + port, client_ssl_creds,
                            client_options);
    client.unary({}, function(err, data) {
      assert.ifError(err);
      done();
    });
  });
  it('Should update metadata with SSL creds', function(done) {
    var metadataUpdater = function(service_url, callback) {
      var metadata = new grpc.Metadata();
      metadata.set('plugin_key', 'plugin_value');
      callback(null, metadata);
    };
    var creds = grpc.credentials.createFromMetadataGenerator(metadataUpdater);
    var combined_creds = grpc.credentials.combineChannelCredentials(
        client_ssl_creds, creds);
    var client = new Client('localhost:' + port, combined_creds,
                            client_options);
    var call = client.unary({}, function(err, data) {
      assert.ifError(err);
    });
    call.on('metadata', function(metadata) {
      assert.deepEqual(metadata.get('plugin_key'), ['plugin_value']);
      done();
    });
  });
  it('Should update metadata for two simultaneous calls', function(done) {
    done = multiDone(done, 2);
    var metadataUpdater = function(service_url, callback) {
      var metadata = new grpc.Metadata();
      metadata.set('plugin_key', 'plugin_value');
      callback(null, metadata);
    };
    var creds = grpc.credentials.createFromMetadataGenerator(metadataUpdater);
    var combined_creds = grpc.credentials.combineChannelCredentials(
        client_ssl_creds, creds);
    var client = new Client('localhost:' + port, combined_creds,
                            client_options);
    var call = client.unary({}, function(err, data) {
      assert.ifError(err);
    });
    call.on('metadata', function(metadata) {
      assert.deepEqual(metadata.get('plugin_key'), ['plugin_value']);
      done();
    });
    var call2 = client.unary({}, function(err, data) {
      assert.ifError(err);
    });
    call2.on('metadata', function(metadata) {
      assert.deepEqual(metadata.get('plugin_key'), ['plugin_value']);
      done();
    });
  });
  it('should propagate errors that the updater emits', function(done) {
    var metadataUpdater = function(service_url, callback) {
      var error = new Error('Authentication error');
      error.code = grpc.status.UNAUTHENTICATED;
      callback(error);
    };
    var creds = grpc.credentials.createFromMetadataGenerator(metadataUpdater);
    var combined_creds = grpc.credentials.combineChannelCredentials(
        client_ssl_creds, creds);
    var client = new Client('localhost:' + port, combined_creds,
                            client_options);
    client.unary({}, function(err, data) {
      assert(err);
      assert.strictEqual(err.message,
                         'Getting metadata from plugin failed with error: ' +
                         'Authentication error');
      assert.strictEqual(err.code, grpc.status.UNAUTHENTICATED);
      done();
    });
  });
  it('should successfully wrap a Google credential', function(done) {
    var creds = grpc.credentials.createFromGoogleCredential(
        fakeSuccessfulGoogleCredentials);
    var combined_creds = grpc.credentials.combineChannelCredentials(
        client_ssl_creds, creds);
    var client = new Client('localhost:' + port, combined_creds,
                            client_options);
    var call = client.unary({}, function(err, data) {
      assert.ifError(err);
    });
    call.on('metadata', function(metadata) {
      assert.deepEqual(metadata.get('authorization'), ['success']);
      done();
    });
  });
  it('Should not add metadata with just SSL credentials', function(done) {
    // Tests idempotency of credentials composition
    var metadataUpdater = function(service_url, callback) {
      var metadata = new grpc.Metadata();
      metadata.set('plugin_key', 'plugin_value');
      callback(null, metadata);
    };
    var creds = grpc.credentials.createFromMetadataGenerator(metadataUpdater);
    grpc.credentials.combineChannelCredentials(client_ssl_creds, creds);
    var client = new Client('localhost:' + port, client_ssl_creds,
                            client_options);
    var call = client.unary({}, function(err, data) {
      assert.ifError(err);
    });
    call.on('metadata', function(metadata) {
      assert.deepEqual(metadata.get('plugin_key'), []);
      done();
    });
  });
  it('should get an error from a Google credential', function(done) {
    var creds = grpc.credentials.createFromGoogleCredential(
        fakeFailingGoogleCredentials);
    var combined_creds = grpc.credentials.combineChannelCredentials(
        client_ssl_creds, creds);
    var client = new Client('localhost:' + port, combined_creds,
                            client_options);
    client.unary({}, function(err, data) {
      assert(err);
      assert.strictEqual(err.message,
                         'Getting metadata from plugin failed with error: ' +
                         'Authentication failure');
      done();
    });
  });
  describe('Per-rpc creds', function() {
    var client;
    var updater_creds;
    before(function() {
      client = new Client('localhost:' + port, client_ssl_creds,
                          client_options);
      var metadataUpdater = function(service_url, callback) {
        var metadata = new grpc.Metadata();
        metadata.set('plugin_key', 'plugin_value');
        callback(null, metadata);
      };
      updater_creds = grpc.credentials.createFromMetadataGenerator(
          metadataUpdater);
    });
    it('Should update metadata on a unary call', function(done) {
      var call = client.unary({}, {credentials: updater_creds},
                              function(err, data) {
                                assert.ifError(err);
                              });
      call.on('metadata', function(metadata) {
        assert.deepEqual(metadata.get('plugin_key'), ['plugin_value']);
        done();
      });
    });
    it('should update metadata on a client streaming call', function(done) {
      var call = client.clientStream({credentials: updater_creds},
                                     function(err, data) {
                                       assert.ifError(err);
                                     });
      call.on('metadata', function(metadata) {
        assert.deepEqual(metadata.get('plugin_key'), ['plugin_value']);
        done();
      });
      call.end();
    });
    it('should update metadata on a server streaming call', function(done) {
      var call = client.serverStream({}, {credentials: updater_creds});
      call.on('data', function() {});
      call.on('metadata', function(metadata) {
        assert.deepEqual(metadata.get('plugin_key'), ['plugin_value']);
        done();
      });
    });
    it('should update metadata on a bidi streaming call', function(done) {
      var call = client.bidiStream({credentials: updater_creds});
      call.on('data', function() {});
      call.on('metadata', function(metadata) {
        assert.deepEqual(metadata.get('plugin_key'), ['plugin_value']);
        done();
      });
      call.end();
    });
    it('should be able to use multiple plugin credentials', function(done) {
      var altMetadataUpdater = function(service_url, callback) {
        var metadata = new grpc.Metadata();
        metadata.set('other_plugin_key', 'other_plugin_value');
        callback(null, metadata);
      };
      var alt_updater_creds = grpc.credentials.createFromMetadataGenerator(
          altMetadataUpdater);
      var combined_updater = grpc.credentials.combineCallCredentials(
          updater_creds, alt_updater_creds);
      var call = client.unary({}, {credentials: combined_updater},
                              function(err, data) {
                                assert.ifError(err);
                              });
      call.on('metadata', function(metadata) {
        assert.deepEqual(metadata.get('plugin_key'), ['plugin_value']);
        assert.deepEqual(metadata.get('other_plugin_key'),
                         ['other_plugin_value']);
        done();
      });
    });
  });
});
