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
var _ = require('lodash');

var surface_client = require('../src/client.js');
var common = require('../src/common');

var ProtoBuf = require('protobufjs');

var grpc = require('..');

var math_proto = new ProtoBuf.Root();
math_proto = math_proto.loadSync(__dirname +
    '/../../proto/math/math.proto', {keepCase: true});

var mathService = math_proto.lookup('math.Math');
var mathServiceAttrs = grpc.loadObject(
    mathService, common.defaultGrpcOptions).service;

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

var server_insecure_creds = grpc.ServerCredentials.createInsecure();

describe('File loader', function() {
  it('Should load a proto file by default', function() {
    assert.doesNotThrow(function() {
      grpc.load(__dirname + '/test_service.proto');
    });
  });
  it('Should load a proto file with the proto format', function() {
    assert.doesNotThrow(function() {
      grpc.load(__dirname + '/test_service.proto', 'proto');
    });
  });
  it('Should load a json file with the json format', function() {
    assert.doesNotThrow(function() {
      grpc.load(__dirname + '/test_service.json', 'json');
    });
  });
});
describe('surface Server', function() {
  var server;
  beforeEach(function() {
    server = new grpc.Server();
  });
  afterEach(function() {
    server.forceShutdown();
  });
  it('should error if started twice', function() {
    server.start();
    assert.throws(function() {
      server.start();
    });
  });
  it('should error if a port is bound after the server starts', function() {
    server.start();
    assert.throws(function() {
      server.bind('localhost:0', grpc.ServerCredentials.createInsecure());
    });
  });
  it('should successfully shutdown if tryShutdown is called', function(done) {
    server.start();
    server.tryShutdown(done);
  });
});
describe('Server.prototype.addProtoService', function() {
  var server;
  var dummyImpls = {
    'div': function() {},
    'divMany': function() {},
    'fib': function() {},
    'sum': function() {}
  };
  beforeEach(function() {
    server = new grpc.Server();
  });
  afterEach(function() {
    server.forceShutdown();
  });
  it('Should succeed with a single proto service', function() {
    assert.doesNotThrow(function() {
      server.addProtoService(mathService, dummyImpls);
    });
  });
  it('Should succeed with a single service attributes object', function() {
    assert.doesNotThrow(function() {
      server.addProtoService(mathServiceAttrs, dummyImpls);
    });
  });
});
describe('Server.prototype.addService', function() {
  var server;
  var dummyImpls = {
    'div': function() {},
    'divMany': function() {},
    'fib': function() {},
    'sum': function() {}
  };
  beforeEach(function() {
    server = new grpc.Server();
  });
  afterEach(function() {
    server.forceShutdown();
  });
  it('Should succeed with a single service', function() {
    assert.doesNotThrow(function() {
      server.addService(mathServiceAttrs, dummyImpls);
    });
  });
  it('Should fail with conflicting method names', function() {
    server.addService(mathServiceAttrs, dummyImpls);
    assert.throws(function() {
      server.addService(mathServiceAttrs, dummyImpls);
    });
  });
  it('Should allow method names as originally written', function() {
    var altDummyImpls = {
      'Div': function() {},
      'DivMany': function() {},
      'Fib': function() {},
      'Sum': function() {}
    };
    assert.doesNotThrow(function() {
      server.addProtoService(mathService, altDummyImpls);
    });
  });
  it('Should have a conflict between name variations', function() {
    /* This is really testing that both name variations are actually used,
       by checking that the method actually gets registered, for the
       corresponding function, in both cases */
    var altDummyImpls = {
      'Div': function() {},
      'DivMany': function() {},
      'Fib': function() {},
      'Sum': function() {}
    };
    server.addProtoService(mathService, altDummyImpls);
    assert.throws(function() {
      server.addProtoService(mathService, dummyImpls);
    });
  });
  it('Should fail if the server has been started', function() {
    server.start();
    assert.throws(function() {
      server.addService(mathServiceAttrs, dummyImpls);
    });
  });
  describe('Default handlers', function() {
    var client;
    beforeEach(function() {
      server.addService(mathServiceAttrs, {});
      var port = server.bind('localhost:0', server_insecure_creds);
      var Client = grpc.loadObject(mathService);
      client = new Client('localhost:' + port,
                          grpc.credentials.createInsecure());
      server.start();
    });
    it('should respond to a unary call with UNIMPLEMENTED', function(done) {
      client.div({divisor: 4, dividend: 3}, function(error, response) {
        assert(error);
        assert.strictEqual(error.code, grpc.status.UNIMPLEMENTED);
        done();
      });
    });
    it('should respond to a client stream with UNIMPLEMENTED', function(done) {
      var call = client.sum(function(error, respones) {
        assert(error);
        assert.strictEqual(error.code, grpc.status.UNIMPLEMENTED);
        done();
      });
      call.end();
    });
    it('should respond to a server stream with UNIMPLEMENTED', function(done) {
      var call = client.fib({limit: 5});
      call.on('data', function(value) {
        assert.fail('No messages expected');
      });
      call.on('error', function(err) {
        assert.strictEqual(err.code, grpc.status.UNIMPLEMENTED);
        done();
      });
      call.on('error', function(status) { /* Do nothing */ });
    });
    it('should respond to a bidi call with UNIMPLEMENTED', function(done) {
      var call = client.divMany();
      call.on('data', function(value) {
        assert.fail('No messages expected');
      });
      call.on('error', function(err) {
        assert.strictEqual(err.code, grpc.status.UNIMPLEMENTED);
        done();
      });
      call.on('error', function(status) { /* Do nothing */ });
      call.end();
    });
  });
});
describe('Client constructor building', function() {
  var illegal_service_attrs = {
    $method : {
      path: '/illegal/$method',
      requestStream: false,
      responseStream: false,
      requestSerialize: _.identity,
      requestDeserialize: _.identity,
      responseSerialize: _.identity,
      responseDeserialize: _.identity
    }
  };
  it('Should reject method names starting with $', function() {
    assert.throws(function() {
      grpc.makeGenericClientConstructor(illegal_service_attrs);
    }, /\$/);
  });
});
describe('waitForClientReady', function() {
  var server;
  var port;
  var Client;
  var client;
  before(function() {
    server = new grpc.Server();
    port = server.bind('localhost:0', grpc.ServerCredentials.createInsecure());
    server.start();
    Client = grpc.loadObject(mathService);
  });
  beforeEach(function() {
    client = new Client('localhost:' + port, grpc.credentials.createInsecure());
  });
  after(function() {
    server.forceShutdown();
  });
  it('should complete when called alone', function(done) {
    grpc.waitForClientReady(client, Infinity, function(error) {
      assert.ifError(error);
      done();
    });
  });
  it('should complete when a call is initiated', function(done) {
    grpc.waitForClientReady(client, Infinity, function(error) {
      assert.ifError(error);
      done();
    });
    var call = client.div({}, function(err, response) {});
    call.cancel();
  });
  it('should complete if called more than once', function(done) {
    done = multiDone(done, 2);
    grpc.waitForClientReady(client, Infinity, function(error) {
      assert.ifError(error);
      done();
    });
    grpc.waitForClientReady(client, Infinity, function(error) {
      assert.ifError(error);
      done();
    });
  });
  it('should complete if called when already ready', function(done) {
    grpc.waitForClientReady(client, Infinity, function(error) {
      assert.ifError(error);
      grpc.waitForClientReady(client, Infinity, function(error) {
        assert.ifError(error);
        done();
      });
    });
  });
  it('should time out if the server does not exist', function(done) {
    var bad_client = new Client('nonexistent_hostname',
                                grpc.credentials.createInsecure());
    var deadline = new Date();
    deadline.setSeconds(deadline.getSeconds() + 1);
    grpc.waitForClientReady(bad_client, deadline, function(error) {
      assert(error);
      done();
    });
  });
});
describe('Echo service', function() {
  var server;
  var client;
  before(function() {
    var test_proto = new ProtoBuf.Root();
    test_proto = test_proto.loadSync(__dirname + '/echo_service.proto',
                                         {keepCase: true});
    var echo_service = test_proto.lookup('EchoService');
    var Client = grpc.loadObject(echo_service);
    server = new grpc.Server();
    server.addService(Client.service, {
      echo: function(call, callback) {
        callback(null, call.request);
      }
    });
    var port = server.bind('localhost:0', server_insecure_creds);
    client = new Client('localhost:' + port, grpc.credentials.createInsecure());
    server.start();
  });
  after(function() {
    server.forceShutdown();
  });
  it('should echo the recieved message directly', function(done) {
    client.echo({value: 'test value', value2: 3}, function(error, response) {
      assert.ifError(error);
      assert.deepEqual(response, {value: 'test value', value2: 3});
      done();
    });
  });
});
describe('Generic client and server', function() {
  function toString(val) {
    return val.toString();
  }
  function toBuffer(str) {
    return new Buffer(str);
  }
  var string_service_attrs = {
    'capitalize' : {
      path: '/string/capitalize',
      requestStream: false,
      responseStream: false,
      requestSerialize: toBuffer,
      requestDeserialize: toString,
      responseSerialize: toBuffer,
      responseDeserialize: toString
    }
  };
  describe('String client and server', function() {
    var client;
    var server;
    before(function() {
      server = new grpc.Server();
      server.addService(string_service_attrs, {
        capitalize: function(call, callback) {
          callback(null, _.capitalize(call.request));
        }
      });
      var port = server.bind('localhost:0', server_insecure_creds);
      server.start();
      var Client = grpc.makeGenericClientConstructor(string_service_attrs);
      client = new Client('localhost:' + port,
                          grpc.credentials.createInsecure());
    });
    after(function() {
      server.forceShutdown();
    });
    it('Should respond with a capitalized string', function(done) {
      client.capitalize('abc', function(err, response) {
        assert.ifError(err);
        assert.strictEqual(response, 'Abc');
        done();
      });
    });
  });
});
describe('Server-side getPeer', function() {
  function toString(val) {
    return val.toString();
  }
  function toBuffer(str) {
    return new Buffer(str);
  }
  var string_service_attrs = {
    'getPeer' : {
      path: '/string/getPeer',
      requestStream: false,
      responseStream: false,
      requestSerialize: toBuffer,
      requestDeserialize: toString,
      responseSerialize: toBuffer,
      responseDeserialize: toString
    }
  };
  var client;
  var server;
  before(function() {
    server = new grpc.Server();
    server.addService(string_service_attrs, {
      getPeer: function(call, callback) {
        try {
          callback(null, call.getPeer());
        } catch (e) {
          call.emit('error', e);
        }
      }
    });
    var port = server.bind('localhost:0', server_insecure_creds);
    server.start();
    var Client = grpc.makeGenericClientConstructor(string_service_attrs);
    client = new Client('localhost:' + port,
                        grpc.credentials.createInsecure());
  });
  after(function() {
    server.forceShutdown();
  });
  it('should respond with a string representing the client', function(done) {
    client.getPeer('', function(err, response) {
      assert.ifError(err);
      // We don't expect a specific value, just that it worked without error
      done();
    });
  });
});
describe('Echo metadata', function() {
  var client;
  var server;
  var metadata;
  before(function() {
    var test_proto = new ProtoBuf.Root();
    test_proto = test_proto.loadSync(__dirname + '/test_service.proto',
                                         {keepCase: true});
    var test_service = test_proto.lookup('TestService');
    var Client = grpc.loadObject(test_service);
    server = new grpc.Server();
    server.addService(Client.service, {
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
    var port = server.bind('localhost:0', server_insecure_creds);
    client = new Client('localhost:' + port, grpc.credentials.createInsecure());
    server.start();
    metadata = new grpc.Metadata();
    metadata.set('key', 'value');
  });
  after(function() {
    server.forceShutdown();
  });
  it('with unary call', function(done) {
    var call = client.unary({}, metadata, function(err, data) {
      assert.ifError(err);
    });
    call.on('metadata', function(metadata) {
      assert.deepEqual(metadata.get('key'), ['value']);
      done();
    });
  });
  it('with client stream call', function(done) {
    var call = client.clientStream(metadata, function(err, data) {
      assert.ifError(err);
    });
    call.on('metadata', function(metadata) {
      assert.deepEqual(metadata.get('key'), ['value']);
      done();
    });
    call.end();
  });
  it('with server stream call', function(done) {
    var call = client.serverStream({}, metadata);
    call.on('data', function() {});
    call.on('metadata', function(metadata) {
      assert.deepEqual(metadata.get('key'), ['value']);
      done();
    });
  });
  it('with bidi stream call', function(done) {
    var call = client.bidiStream(metadata);
    call.on('data', function() {});
    call.on('metadata', function(metadata) {
      assert.deepEqual(metadata.get('key'), ['value']);
      done();
    });
    call.end();
  });
  it('shows the correct user-agent string', function(done) {
    var version = require('../../../package.json').version;
    var call = client.unary({}, metadata,
                            function(err, data) { assert.ifError(err); });
    call.on('metadata', function(metadata) {
      assert(_.startsWith(metadata.get('user-agent')[0],
                          'grpc-node/' + version));
      done();
    });
  });
  it('properly handles duplicate values', function(done) {
    var dup_metadata = metadata.clone();
    dup_metadata.add('key', 'value2');
    var call = client.unary({}, dup_metadata,
                            function(err, data) {assert.ifError(err); });
    call.on('metadata', function(resp_metadata) {
      // Two arrays are equal iff their symmetric difference is empty
      assert.deepEqual(_.xor(dup_metadata.get('key'), resp_metadata.get('key')),
                       []);
      done();
    });
  });
});
describe('Client malformed response handling', function() {
  var server;
  var client;
  var badArg = new Buffer([0xFF]);
  before(function() {
    var test_proto = new ProtoBuf.Root();
    test_proto = test_proto.loadSync(__dirname + '/test_service.proto',
                                         {keepCase: true});
    var test_service = test_proto.lookup('TestService');
    var malformed_test_service = {
      unary: {
        path: '/TestService/Unary',
        requestStream: false,
        responseStream: false,
        requestDeserialize: _.identity,
        responseSerialize: _.identity
      },
      clientStream: {
        path: '/TestService/ClientStream',
        requestStream: true,
        responseStream: false,
        requestDeserialize: _.identity,
        responseSerialize: _.identity
      },
      serverStream: {
        path: '/TestService/ServerStream',
        requestStream: false,
        responseStream: true,
        requestDeserialize: _.identity,
        responseSerialize: _.identity
      },
      bidiStream: {
        path: '/TestService/BidiStream',
        requestStream: true,
        responseStream: true,
        requestDeserialize: _.identity,
        responseSerialize: _.identity
      }
    };
    server = new grpc.Server();
    server.addService(malformed_test_service, {
      unary: function(call, cb) {
        cb(null, badArg);
      },
      clientStream: function(stream, cb) {
        stream.on('data', function() {/* Ignore requests */});
        stream.on('end', function() {
          cb(null, badArg);
        });
      },
      serverStream: function(stream) {
        stream.write(badArg);
        stream.end();
      },
      bidiStream: function(stream) {
        stream.on('data', function() {
          // Ignore requests
          stream.write(badArg);
        });
        stream.on('end', function() {
          stream.end();
        });
      }
    });
    var port = server.bind('localhost:0', server_insecure_creds);
    var Client = grpc.loadObject(test_service);
    client = new Client('localhost:' + port, grpc.credentials.createInsecure());
    server.start();
  });
  after(function() {
    server.forceShutdown();
  });
  it('should get an INTERNAL status with a unary call', function(done) {
    client.unary({}, function(err, data) {
      assert(err);
      assert.strictEqual(err.code, grpc.status.INTERNAL);
      done();
    });
  });
  it('should get an INTERNAL status with a client stream call', function(done) {
    var call = client.clientStream(function(err, data) {
      assert(err);
      assert.strictEqual(err.code, grpc.status.INTERNAL);
      done();
    });
    call.write({});
    call.end();
  });
  it('should get an INTERNAL status with a server stream call', function(done) {
    var call = client.serverStream({});
    call.on('data', function(){});
    call.on('error', function(err) {
      assert.strictEqual(err.code, grpc.status.INTERNAL);
      done();
    });
  });
  it('should get an INTERNAL status with a bidi stream call', function(done) {
    var call = client.bidiStream();
    call.on('data', function(){});
    call.on('error', function(err) {
      assert.strictEqual(err.code, grpc.status.INTERNAL);
      done();
    });
    call.write({});
    call.end();
  });
});
describe('Server serialization failure handling', function() {
  function serializeFail(obj) {
    throw new Error('Serialization failed');
  }
  var client;
  var server;
  before(function() {
    var test_proto = new ProtoBuf.Root();
    test_proto = test_proto.loadSync(__dirname + '/test_service.proto',
                                         {keepCase: true});
    var test_service = test_proto.lookup('TestService');
    var malformed_test_service = {
      unary: {
        path: '/TestService/Unary',
        requestStream: false,
        responseStream: false,
        requestDeserialize: _.identity,
        responseSerialize: serializeFail
      },
      clientStream: {
        path: '/TestService/ClientStream',
        requestStream: true,
        responseStream: false,
        requestDeserialize: _.identity,
        responseSerialize: serializeFail
      },
      serverStream: {
        path: '/TestService/ServerStream',
        requestStream: false,
        responseStream: true,
        requestDeserialize: _.identity,
        responseSerialize: serializeFail
      },
      bidiStream: {
        path: '/TestService/BidiStream',
        requestStream: true,
        responseStream: true,
        requestDeserialize: _.identity,
        responseSerialize: serializeFail
      }
    };
    server = new grpc.Server();
    server.addService(malformed_test_service, {
      unary: function(call, cb) {
        cb(null, {});
      },
      clientStream: function(stream, cb) {
        stream.on('data', function() {/* Ignore requests */});
        stream.on('end', function() {
          cb(null, {});
        });
      },
      serverStream: function(stream) {
        stream.write({});
        stream.end();
      },
      bidiStream: function(stream) {
        stream.on('data', function() {
          // Ignore requests
          stream.write({});
        });
        stream.on('end', function() {
          stream.end();
        });
      }
    });
    var port = server.bind('localhost:0', server_insecure_creds);
    var Client = grpc.loadObject(test_service);
    client = new Client('localhost:' + port, grpc.credentials.createInsecure());
    server.start();
  });
  after(function() {
    server.forceShutdown();
  });
  it('should get an INTERNAL status with a unary call', function(done) {
    client.unary({}, function(err, data) {
      assert(err);
      assert.strictEqual(err.code, grpc.status.INTERNAL);
      done();
    });
  });
  it('should get an INTERNAL status with a client stream call', function(done) {
    var call = client.clientStream(function(err, data) {
      assert(err);
      assert.strictEqual(err.code, grpc.status.INTERNAL);
      done();
    });
    call.write({});
    call.end();
  });
  it('should get an INTERNAL status with a server stream call', function(done) {
    var call = client.serverStream({});
    call.on('data', function(){});
    call.on('error', function(err) {
      assert.strictEqual(err.code, grpc.status.INTERNAL);
      done();
    });
  });
  it('should get an INTERNAL status with a bidi stream call', function(done) {
    var call = client.bidiStream();
    call.on('data', function(){});
    call.on('error', function(err) {
      assert.strictEqual(err.code, grpc.status.INTERNAL);
      done();
    });
    call.write({});
    call.end();
  });
});
describe('Other conditions', function() {
  var test_service;
  var Client;
  var client;
  var server;
  var port;
  before(function() {
    var test_proto = new ProtoBuf.Root();
    test_proto = test_proto.loadSync(__dirname + '/test_service.proto',
                                         {keepCase: true});
    test_service = test_proto.lookup('TestService');
    Client = grpc.loadObject(test_service);
    server = new grpc.Server();
    var trailer_metadata = new grpc.Metadata();
    trailer_metadata.add('trailer-present', 'yes');
    server.addService(Client.service, {
      unary: function(call, cb) {
        var req = call.request;
        if (req.error) {
          cb({code: grpc.status.UNKNOWN,
              details: 'Requested error'}, null, trailer_metadata);
        } else {
          cb(null, {count: 1}, trailer_metadata);
        }
      },
      clientStream: function(stream, cb){
        var count = 0;
        var errored;
        stream.on('data', function(data) {
          if (data.error) {
            errored = true;
            cb(new Error('Requested error'), null, trailer_metadata);
          } else {
            count += 1;
          }
        });
        stream.on('end', function() {
          if (!errored) {
            cb(null, {count: count}, trailer_metadata);
          }
        });
      },
      serverStream: function(stream) {
        var req = stream.request;
        if (req.error) {
          var err = {code: grpc.status.UNKNOWN,
                     details: 'Requested error'};
          err.metadata = trailer_metadata;
          stream.emit('error', err);
        } else {
          for (var i = 0; i < 5; i++) {
            stream.write({count: i});
          }
          stream.end(trailer_metadata);
        }
      },
      bidiStream: function(stream) {
        var count = 0;
        stream.on('data', function(data) {
          if (data.error) {
            var err = new Error('Requested error');
            err.metadata = trailer_metadata.clone();
            err.metadata.add('count', '' + count);
            stream.emit('error', err);
          } else {
            stream.write({count: count});
            count += 1;
          }
        });
        stream.on('end', function() {
          stream.end(trailer_metadata);
        });
      }
    });
    port = server.bind('localhost:0', server_insecure_creds);
    client = new Client('localhost:' + port, grpc.credentials.createInsecure());
    server.start();
  });
  after(function() {
    server.forceShutdown();
  });
  it('channel.getTarget should be available', function() {
    assert.strictEqual(typeof grpc.getClientChannel(client).getTarget(),
                       'string');
  });
  it('client should be able to pause and resume a stream', function(done) {
    var call = client.bidiStream();
    call.on('data', function(data) {
      assert(data.count < 3);
      call.pause();
      setTimeout(function() {
        call.resume();
      }, 10);
    });
    call.on('end', function() {
      done();
    });
    call.write({});
    call.write({});
    call.write({});
    call.end();
  });
  describe('Server recieving bad input', function() {
    var misbehavingClient;
    var badArg = new Buffer([0xFF]);
    before(function() {
      var test_service_attrs = {
        unary: {
          path: '/TestService/Unary',
          requestStream: false,
          responseStream: false,
          requestSerialize: _.identity,
          responseDeserialize: _.identity
        },
        clientStream: {
          path: '/TestService/ClientStream',
          requestStream: true,
          responseStream: false,
          requestSerialize: _.identity,
          responseDeserialize: _.identity
        },
        serverStream: {
          path: '/TestService/ServerStream',
          requestStream: false,
          responseStream: true,
          requestSerialize: _.identity,
          responseDeserialize: _.identity
        },
        bidiStream: {
          path: '/TestService/BidiStream',
          requestStream: true,
          responseStream: true,
          requestSerialize: _.identity,
          responseDeserialize: _.identity
        }
      };
      var Client = surface_client.makeClientConstructor(test_service_attrs,
                                                        'TestService');
      misbehavingClient = new Client('localhost:' + port,
                                     grpc.credentials.createInsecure());
    });
    it('should respond correctly to a unary call', function(done) {
      misbehavingClient.unary(badArg, function(err, data) {
        assert(err);
        assert.strictEqual(err.code, grpc.status.INTERNAL);
        done();
      });
    });
    it('should respond correctly to a client stream', function(done) {
      var call = misbehavingClient.clientStream(function(err, data) {
        assert(err);
        assert.strictEqual(err.code, grpc.status.INTERNAL);
        done();
      });
      call.write(badArg);
      // TODO(mlumish): Remove call.end()
      call.end();
    });
    it('should respond correctly to a server stream', function(done) {
      var call = misbehavingClient.serverStream(badArg);
      call.on('data', function(data) {
        assert.fail(data, null, 'Unexpected data', '===');
      });
      call.on('error', function(err) {
        assert.strictEqual(err.code, grpc.status.INTERNAL);
        done();
      });
    });
    it('should respond correctly to a bidi stream', function(done) {
      var call = misbehavingClient.bidiStream();
      call.on('data', function(data) {
        assert.fail(data, null, 'Unexpected data', '===');
      });
      call.on('error', function(err) {
        assert.strictEqual(err.code, grpc.status.INTERNAL);
        done();
      });
      call.write(badArg);
      // TODO(mlumish): Remove call.end()
      call.end();
    });
  });
  describe('Trailing metadata', function() {
    it('should be present when a unary call succeeds', function(done) {
      var call = client.unary({error: false}, function(err, data) {
        assert.ifError(err);
      });
      call.on('status', function(status) {
        assert.deepEqual(status.metadata.get('trailer-present'), ['yes']);
        done();
      });
    });
    it('should be present when a unary call fails', function(done) {
      var call = client.unary({error: true}, function(err, data) {
        assert(err);
      });
      call.on('status', function(status) {
        assert.deepEqual(status.metadata.get('trailer-present'), ['yes']);
        done();
      });
    });
    it('should be present when a client stream call succeeds', function(done) {
      var call = client.clientStream(function(err, data) {
        assert.ifError(err);
      });
      call.write({error: false});
      call.write({error: false});
      call.end();
      call.on('status', function(status) {
        assert.deepEqual(status.metadata.get('trailer-present'), ['yes']);
        done();
      });
    });
    it('should be present when a client stream call fails', function(done) {
      var call = client.clientStream(function(err, data) {
        assert(err);
      });
      call.write({error: false});
      call.write({error: true});
      call.end();
      call.on('status', function(status) {
        assert.deepEqual(status.metadata.get('trailer-present'), ['yes']);
        done();
      });
    });
    it('should be present when a server stream call succeeds', function(done) {
      var call = client.serverStream({error: false});
      call.on('data', function(){});
      call.on('status', function(status) {
        assert.strictEqual(status.code, grpc.status.OK);
        assert.deepEqual(status.metadata.get('trailer-present'), ['yes']);
        done();
      });
    });
    it('should be present when a server stream call fails', function(done) {
      var call = client.serverStream({error: true});
      call.on('data', function(){});
      call.on('error', function(error) {
        assert.deepEqual(error.metadata.get('trailer-present'), ['yes']);
        done();
      });
    });
    it('should be present when a bidi stream succeeds', function(done) {
      var call = client.bidiStream();
      call.write({error: false});
      call.write({error: false});
      call.end();
      call.on('data', function(){});
      call.on('status', function(status) {
        assert.strictEqual(status.code, grpc.status.OK);
        assert.deepEqual(status.metadata.get('trailer-present'), ['yes']);
        done();
      });
    });
    it('should be present when a bidi stream fails', function(done) {
      var call = client.bidiStream();
      call.write({error: false});
      call.write({error: true});
      call.end();
      call.on('data', function(){});
      call.on('error', function(error) {
        assert.deepEqual(error.metadata.get('trailer-present'), ['yes']);
        done();
      });
    });
  });
  describe('Error object should contain the status', function() {
    it('for a unary call', function(done) {
      client.unary({error: true}, function(err, data) {
        assert(err);
        assert.strictEqual(err.code, grpc.status.UNKNOWN);
        assert.strictEqual(err.message, 'Requested error');
        done();
      });
    });
    it('for a client stream call', function(done) {
      var call = client.clientStream(function(err, data) {
        assert(err);
        assert.strictEqual(err.code, grpc.status.UNKNOWN);
        assert.strictEqual(err.message, 'Requested error');
        done();
      });
      call.write({error: false});
      call.write({error: true});
      call.end();
    });
    it('for a server stream call', function(done) {
      var call = client.serverStream({error: true});
      call.on('data', function(){});
      call.on('error', function(error) {
        assert.strictEqual(error.code, grpc.status.UNKNOWN);
        assert.strictEqual(error.message, 'Requested error');
        done();
      });
    });
    it('for a bidi stream call', function(done) {
      var call = client.bidiStream();
      call.write({error: false});
      call.write({error: true});
      call.end();
      call.on('data', function(){});
      call.on('error', function(error) {
        assert.strictEqual(error.code, grpc.status.UNKNOWN);
        assert.strictEqual(error.message, 'Requested error');
        done();
      });
    });
  });
  describe('call.getPeer should return the peer', function() {
    it('for a unary call', function(done) {
      var call = client.unary({error: false}, function(err, data) {
        assert.ifError(err);
        done();
      });
      assert.strictEqual(typeof call.getPeer(), 'string');
    });
    it('for a client stream call', function(done) {
      var call = client.clientStream(function(err, data) {
        assert.ifError(err);
        done();
      });
      assert.strictEqual(typeof call.getPeer(), 'string');
      call.write({error: false});
      call.end();
    });
    it('for a server stream call', function(done) {
      var call = client.serverStream({error: false});
      assert.strictEqual(typeof call.getPeer(), 'string');
      call.on('data', function(){});
      call.on('status', function(status) {
        assert.strictEqual(status.code, grpc.status.OK);
        done();
      });
    });
    it('for a bidi stream call', function(done) {
      var call = client.bidiStream();
      assert.strictEqual(typeof call.getPeer(), 'string');
      call.write({error: false});
      call.end();
      call.on('data', function(){});
      call.on('status', function(status) {
        done();
      });
    });
  });
});
describe('Call propagation', function() {
  var proxy;
  var proxy_impl;

  var test_service;
  var Client;
  var client;
  var server;
  before(function() {
    var test_proto = new ProtoBuf.Root();
    test_proto = test_proto.loadSync(__dirname + '/test_service.proto',
                                         {keepCase: true});
    test_service = test_proto.lookup('TestService');
    server = new grpc.Server();
    Client = grpc.loadObject(test_service);
    server.addService(Client.service, {
      unary: function(call) {},
      clientStream: function(stream) {},
      serverStream: function(stream) {},
      bidiStream: function(stream) {}
    });
    var port = server.bind('localhost:0', server_insecure_creds);
    client = new Client('localhost:' + port, grpc.credentials.createInsecure());
    server.start();
  });
  after(function() {
    server.forceShutdown();
  });
  beforeEach(function() {
    proxy = new grpc.Server();
    proxy_impl = {
      unary: function(call) {},
      clientStream: function(stream) {},
      serverStream: function(stream) {},
      bidiStream: function(stream) {}
    };
  });
  afterEach(function() {
    proxy.forceShutdown();
  });
  describe('Cancellation', function() {
    it('With a unary call', function(done) {
      done = multiDone(done, 2);
      var call;
      proxy_impl.unary = function(parent, callback) {
        client.unary(parent.request, {parent: parent}, function(err, value) {
          try {
            assert(err);
            assert.strictEqual(err.code, grpc.status.CANCELLED);
          } finally {
            callback(err, value);
            done();
          }
        });
        call.cancel();
      };
      proxy.addService(Client.service, proxy_impl);
      var proxy_port = proxy.bind('localhost:0', server_insecure_creds);
      proxy.start();
      var proxy_client = new Client('localhost:' + proxy_port,
                                    grpc.credentials.createInsecure());
      call = proxy_client.unary({}, function(err, value) { done(); });
    });
    it('With a client stream call', function(done) {
      done = multiDone(done, 2);
      var call;
      proxy_impl.clientStream = function(parent, callback) {
        client.clientStream({parent: parent}, function(err, value) {
          try {
            assert(err);
            assert.strictEqual(err.code, grpc.status.CANCELLED);
          } finally {
            callback(err, value);
            done();
          }
        });
        call.cancel();
      };
      proxy.addService(Client.service, proxy_impl);
      var proxy_port = proxy.bind('localhost:0', server_insecure_creds);
      proxy.start();
      var proxy_client = new Client('localhost:' + proxy_port,
                                    grpc.credentials.createInsecure());
      call = proxy_client.clientStream(function(err, value) { done(); });
    });
    it('With a server stream call', function(done) {
      done = multiDone(done, 2);
      var call;
      proxy_impl.serverStream = function(parent) {
        var child = client.serverStream(parent.request, {parent: parent});
        child.on('data', function() {});
        child.on('error', function(err) {
          assert(err);
          assert.strictEqual(err.code, grpc.status.CANCELLED);
          done();
        });
        call.cancel();
      };
      proxy.addService(Client.service, proxy_impl);
      var proxy_port = proxy.bind('localhost:0', server_insecure_creds);
      proxy.start();
      var proxy_client = new Client('localhost:' + proxy_port,
                                    grpc.credentials.createInsecure());
      call = proxy_client.serverStream({});
      call.on('data', function() {});
      call.on('error', function(err) {
        done();
      });
    });
    it('With a bidi stream call', function(done) {
      done = multiDone(done, 2);
      var call;
      proxy_impl.bidiStream = function(parent) {
        var child = client.bidiStream({parent: parent});
        child.on('data', function() {});
        child.on('error', function(err) {
          assert(err);
          assert.strictEqual(err.code, grpc.status.CANCELLED);
          done();
        });
        call.cancel();
      };
      proxy.addService(Client.service, proxy_impl);
      var proxy_port = proxy.bind('localhost:0', server_insecure_creds);
      proxy.start();
      var proxy_client = new Client('localhost:' + proxy_port,
                                    grpc.credentials.createInsecure());
      call = proxy_client.bidiStream();
      call.on('data', function() {});
      call.on('error', function(err) {
        done();
      });
    });
  });
  describe('Deadline', function() {
    /* jshint bitwise:false */
    var deadline_flags = (grpc.propagate.DEFAULTS &
        ~grpc.propagate.CANCELLATION);
    it('With a client stream call', function(done) {
      done = multiDone(done, 2);
      proxy_impl.clientStream = function(parent, callback) {
        var options = {parent: parent, propagate_flags: deadline_flags};
        client.clientStream(options, function(err, value) {
          try {
            assert(err);
            assert(err.code === grpc.status.DEADLINE_EXCEEDED ||
                err.code === grpc.status.INTERNAL);
          } finally {
            callback(err, value);
            done();
          }
        });
      };
      proxy.addService(Client.service, proxy_impl);
      var proxy_port = proxy.bind('localhost:0', server_insecure_creds);
      proxy.start();
      var proxy_client = new Client('localhost:' + proxy_port,
                                    grpc.credentials.createInsecure());
      var deadline = new Date();
      deadline.setSeconds(deadline.getSeconds() + 1);
      proxy_client.clientStream({deadline: deadline}, function(err, value) {
        done();
      });
    });
    it('With a bidi stream call', function(done) {
      done = multiDone(done, 2);
      proxy_impl.bidiStream = function(parent) {
        var child = client.bidiStream(
            {parent: parent, propagate_flags: deadline_flags});
        child.on('data', function() {});
        child.on('error', function(err) {
          assert(err);
          assert(err.code === grpc.status.DEADLINE_EXCEEDED ||
              err.code === grpc.status.INTERNAL);
          done();
        });
      };
      proxy.addService(Client.service, proxy_impl);
      var proxy_port = proxy.bind('localhost:0', server_insecure_creds);
      proxy.start();
      var proxy_client = new Client('localhost:' + proxy_port,
                                    grpc.credentials.createInsecure());
      var deadline = new Date();
      deadline.setSeconds(deadline.getSeconds() + 1);
      var call = proxy_client.bidiStream({deadline: deadline});
      call.on('data', function() {});
      call.on('error', function(err) {
        done();
      });
    });
  });
});
describe('Cancelling surface client', function() {
  var client;
  var server;
  before(function() {
    server = new grpc.Server();
    server.addService(mathServiceAttrs, {
      'div': function(stream) {},
      'divMany': function(stream) {},
      'fib': function(stream) {},
      'sum': function(stream) {}
    });
    var port = server.bind('localhost:0', server_insecure_creds);
    var Client = surface_client.makeClientConstructor(mathServiceAttrs);
    client = new Client('localhost:' + port, grpc.credentials.createInsecure());
    server.start();
  });
  after(function() {
    server.forceShutdown();
  });
  it('Should correctly cancel a unary call', function(done) {
    var call = client.div({'divisor': 0, 'dividend': 0}, function(err, resp) {
      assert.strictEqual(err.code, surface_client.status.CANCELLED);
      done();
    });
    call.cancel();
  });
  it('Should correctly cancel a client stream call', function(done) {
    var call = client.sum(function(err, resp) {
      assert.strictEqual(err.code, surface_client.status.CANCELLED);
      done();
    });
    call.cancel();
  });
  it('Should correctly cancel a server stream call', function(done) {
    var call = client.fib({'limit': 5});
    call.on('data', function() {});
    call.on('error', function(error) {
      assert.strictEqual(error.code, surface_client.status.CANCELLED);
      done();
    });
    call.cancel();
  });
  it('Should correctly cancel a bidi stream call', function(done) {
    var call = client.divMany();
    call.on('data', function() {});
    call.on('error', function(error) {
      assert.strictEqual(error.code, surface_client.status.CANCELLED);
      done();
    });
    call.cancel();
  });
});
