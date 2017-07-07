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
var assert = require('assert');
var grpc = require('..');
var grpc_client = require('../src/client.js');
var Metadata = require('../src/metadata');

var insecureCreds = grpc.credentials.createInsecure();

var echo_proto = grpc.load(__dirname + '/echo_service.proto');
var echo_service = echo_proto.EchoService.service;

var StatusBuilder = grpc_client.StatusBuilder;
var ListenerBuilder = grpc_client.ListenerBuilder;
var InterceptingCall = grpc_client.InterceptingCall;
var RequesterBuilder = grpc_client.RequesterBuilder;

var InterceptorProvider = grpc_client.InterceptorProvider;

var CallRegistry = function(done, expectation, is_ordered) {
  this.call_map = {};
  this.call_array = [];
  this.done = done;
  this.expectation = expectation;
  this.expectation_is_array = _.isArray(this.expectation);
  this.is_ordered = is_ordered;
};

CallRegistry.prototype.addCall = function(call_name) {
  if (this.expectation_is_array) {
    this.call_array.push(call_name);
  } else {
    if (!this.call_map[call_name]) {
      this.call_map[call_name] = 0;
    }
    this.call_map[call_name]++;
  }
  this.maybeCallDone();
};

CallRegistry.prototype.maybeCallDone = function() {
  if (this.expectation_is_array) {
    if (this.is_ordered) {
      if (this.expectation && _.isEqual(this.expectation, this.call_array)) {
        this.done();
      }
    } else {
      var intersection = _.intersectionWith(this.expectation, this.call_array,
        _.isEqual);
      if (intersection.length === this.expectation.length) {
        this.done();
      }
    }
  } else if (this.expectation && _.isEqual(this.expectation, this.call_map)) {
    this.done();
  }
};


describe('Client interceptors', function() {
  var echo_server;
  var echo_port;
  var client;

  function startServer() {
    echo_server = new grpc.Server();
    echo_server.addService(echo_service, {
      echo: function(call, callback) {
        call.sendMetadata(call.metadata);
        if (call.request.value === 'error') {
          var status = {
            code: 2,
            message: 'test status message'
          };
          status.metadata = call.metadata;
          callback(status, null);
          return;
        }
        callback(null, call.request);
      },
      echoClientStream: function(call, callback){
        call.sendMetadata(call.metadata);
        var payload;
        var err = null;
        call.on('data', function(data) {
          if (data.value === 'error') {
            err = {
              code: 2,
              message: 'test status message'
            };
            err.metadata = call.metadata;
            return;
          }
          payload = data;
        });
        call.on('end', function() {
          callback(err, payload, call.metadata);
        });
      },
      echoServerStream: function(call) {
        call.sendMetadata(call.metadata);
        if (call.request.value === 'error') {
          var status = {
            code: 2,
            message: 'test status message'
          };
          status.metadata = call.metadata;
          call.emit('error', status);
          return;
        }
        call.write(call.request);
        call.end(call.metadata);
      },
      echoBidiStream: function(call) {
        call.sendMetadata(call.metadata);
        call.on('data', function(data) {
          if (data.value === 'error') {
            var status = {
              code: 2,
              message: 'test status message'
            };
            call.emit('error', status);
            return;
          }
          call.write(data);
        });
        call.on('end', function() {
          call.end(call.metadata);
        });
      }
    });
    var server_credentials = grpc.ServerCredentials.createInsecure();
    echo_port = echo_server.bind('localhost:0', server_credentials);
    echo_server.start();
  }

  function stopServer() {
    echo_server.forceShutdown();
  }

  function resetClient() {
    var EchoClient = grpc_client.makeClientConstructor(echo_service);
    client = new EchoClient('localhost:' + echo_port, insecureCreds);
  }

  before(function() {
    startServer();
  });
  beforeEach(function() {
    resetClient();
  });
  after(function() {
    stopServer();
  });
  describe('pass calls through when no interceptors provided', function() {
    it('with unary call', function(done) {
      var expected_value = 'foo';
      var message = {value: expected_value};
      client.echo(message, function(err, response) {
        assert.strictEqual(response.value, expected_value);
        done();
      });
      assert(_.isEqual(grpc_client.getClientInterceptors(client), {
        echo: [],
        echoClientStream: [],
        echoServerStream: [],
        echoBidiStream: []
      }));
    });
  });

  describe('execute downstream interceptors when a new call is made outbound',
    function() {
      var registry;
      var options;
      var stored_listener;
      var stored_metadata;
      var interceptor_a = function(options, nextCall) {
        options.call_number = 1;
        registry.addCall('construct a ' + options.call_number);
        return new InterceptingCall(nextCall(options), {
          start: function(metadata, listener, next) {
            registry.addCall('start a ' + options.call_number);
            stored_listener = listener;
            stored_metadata = metadata;
            next(metadata, listener);
          },
          sendMessage: function(message, next) {
            registry.addCall('send a ' + options.call_number);
            var options2 = _.clone(options);
            options2.call_number = 2;
            var second_call = nextCall(options2);
            second_call.start(stored_metadata, stored_listener);
            second_call.sendMessage(message);
            second_call.halfClose();
            next(message);
          },
          halfClose: function(next) {
            registry.addCall('close a ' + options.call_number);
            next();
          }
        });
      };

      var interceptor_b = function(options, nextCall) {
        registry.addCall('construct b ' + options.call_number);
        return new InterceptingCall(nextCall(options), {
          start: function(metadata, listener, next) {
            registry.addCall('start b ' + options.call_number);
            next(metadata, listener);
          },
          sendMessage: function(message, next) {
            registry.addCall('send b ' + options.call_number);
            next(message);
          },
          halfClose: function(next) {
            registry.addCall('close b ' + options.call_number);
            next();
          }
        });
      };

      // Prevents async 'cancelled' status from server breaking later tests
      var cleanup_interceptor = function(options, nextCall) {
        return new InterceptingCall(nextCall(options), {
          start: function(metadata, listener, next) {
            next(metadata, {
              onReceiveStatus: function(status, next) {
                next({code: 0});
              }
            });
          }
        });
      };

      options = {
        interceptors: [interceptor_a, interceptor_b, cleanup_interceptor]
      };

      var expected_calls = [
        'construct a 1',
        'construct b 1',
        'start a 1',
        'start b 1',
        'send a 1',
        'construct b 2',
        'start b 2',
        'send b 2',
        'close b 2',
        'send b 1',
        'close a 1',
        'close b 1',
        'response'
      ];

      it('with unary call', function(done) {
        registry = new CallRegistry( done, expected_calls, true);
        var message = {};
        message.value = 'foo';
        client.echo(message, options, function(err, response){
          if (!err) {
            registry.addCall('response');
          }
        });
      });
      it('with client streaming call', function(done) {
        registry = new CallRegistry( done, expected_calls, true);
        var message = {};
        message.value = 'foo';
        var stream = client.echoClientStream(options, function(err, response) {
          if (!err) {
            registry.addCall('response');
          }
        });
        stream.write(message);
        stream.end();
      });
      it('with server streaming call', function(done) {
        registry = new CallRegistry( done, expected_calls, true);
        var message = {};
        message.value = 'foo';
        var stream = client.echoServerStream(message, options);
        stream.on('data', function(data) {
          registry.addCall('response');
        });
      });
      it('with bidi streaming call', function(done) {
        registry = new CallRegistry( done, expected_calls, true);
        var message = {};
        message.value = 'foo';
        var stream = client.echoBidiStream(options);
        stream.on('data', function(data) {
          registry.addCall('response');
        });
        stream.write(message);
        stream.end();
      });
    });


  describe('execute downstream interceptors when a new call is made inbound',
    function() {
      var registry;
      var interceptor_a = function(options, nextCall) {
        return new InterceptingCall(nextCall(options), {
          start: function(metadata, listener, next) {
            next(metadata, {
              onReceiveMetadata: function() {},
              onReceiveMessage: function(message, next) {
                registry.addCall('interceptor_a');
                var second_call = nextCall(options);
                second_call.start(metadata, listener);
                second_call.sendMessage(message);
                second_call.halfClose();
              },
              onReceiveStatus: function() {}
            });
          }
        });
      };

      var interceptor_b = function(options, nextCall) {
        return new InterceptingCall(nextCall(options), {
          start: function(metadata, listener, next) {
            next(metadata, {
              onReceiveMessage: function(message, next) {
                registry.addCall('interceptor_b');
                next(message);
              }
            });
          }
        });
      };

      var options = {
        interceptors: [interceptor_a, interceptor_b]
      };

      var expected_calls = ['interceptor_b', 'interceptor_a',
            'interceptor_b', 'response'];
      it('with unary call', function(done) {
        registry = new CallRegistry(done, expected_calls, true);
        var message = {};
        message.value = 'foo';
        client.echo(message, options, function(err) {
          if (!err) {
            registry.addCall('response');
          }
        });
      });
      it('with client streaming call', function(done) {
        registry = new CallRegistry(done, expected_calls, true);
        var message = {};
        message.value = 'foo';
        var stream = client.echoClientStream(options, function(err, response) {
          if (!err) {
            registry.addCall('response');
          }
        });
        stream.write(message);
        stream.end();
      });
    });

  it('will delay operations and short circuit unary requests', function(done) {
    var registry = new CallRegistry(done, ['foo_miss', 'foo_hit', 'bar_miss',
      'foo_hit_done', 'foo_miss_done', 'bar_miss_done']);
    var cache = {};
    var _getCachedResponse = function(value) {
      return cache[value];
    };
    var _store = function(key, value) {
      cache[key] = value;
    };

    var interceptor = function(options, nextCall) {
      var savedMetadata;
      var startNext;
      var storedListener;
      var storedMessage;
      var messageNext;
      var requester = (new RequesterBuilder())
        .withStart(function(metadata, listener, next) {
          savedMetadata = metadata;
          storedListener = listener;
          startNext = next;
        })
        .withSendMessage(function(message, next) {
          storedMessage = message;
          messageNext = next;
        })
        .withHalfClose(function(next) {
          var cachedValue = _getCachedResponse(storedMessage.value);
          if (cachedValue) {
            var cachedMessage = {};
            cachedMessage.value = cachedValue;
            registry.addCall(storedMessage.value + '_hit');
            storedListener.onReceiveMetadata(new Metadata());
            storedListener.onReceiveMessage(cachedMessage);
            storedListener.onReceiveStatus(
              (new StatusBuilder()).withCode(grpc.status.OK).build());
          } else {
            registry.addCall(storedMessage.value + '_miss');
            var newListener = (new ListenerBuilder()).withOnReceiveMessage(
              function(message, next) {
                _store(storedMessage.value, message.value);
                next(message);
              }).build();
            startNext(savedMetadata, newListener);
            messageNext(storedMessage);
            next();
          }
        })
        .withCancel(function(message, next) {
          next();
        }).build();

      return new InterceptingCall(nextCall(options), requester);
    };

    var options = {
      interceptors: [interceptor]
    };

    var foo_message = {};
    foo_message.value = 'foo';
    client.echo(foo_message, options, function(err, response){
      assert.equal(response.value, 'foo');
      registry.addCall('foo_miss_done');
      client.echo(foo_message, options, function(err, response){
        assert.equal(response.value, 'foo');
        registry.addCall('foo_hit_done');
      });
    });

    var bar_message = {};
    bar_message.value = 'bar';
    client.echo(bar_message, options, function(err, response) {
      assert.equal(response.value, 'bar');
      registry.addCall('bar_miss_done');
    });
  });

  it('can retry failed messages and handle eventual success', function(done) {
    var registry = new CallRegistry(done,
      ['retry_foo_1', 'retry_foo_2', 'retry_foo_3', 'foo_result',
        'retry_bar_1', 'bar_result']);
    var maxRetries = 3;
    var retry_interceptor = function(options, nextCall) {
      var savedMetadata;
      var savedSendMessage;
      var savedReceiveMessage;
      var savedMessageNext;
      var requester = (new RequesterBuilder())
        .withStart(function(metadata, listener, next) {
          savedMetadata = metadata;
          var new_listener = (new ListenerBuilder())
            .withOnReceiveMessage(function(message, next) {
              savedReceiveMessage = message;
              savedMessageNext = next;
            })
            .withOnReceiveStatus(function(status, next) {
                var retries = 0;
                var retry = function(message, metadata) {
                  retries++;
                  var newCall = nextCall(options);
                  var receivedMessage;
                  newCall.start(metadata, {
                    onReceiveMessage: function(message) {
                      receivedMessage = message;
                    },
                    onReceiveStatus: function(status) {
                      registry.addCall('retry_' + savedMetadata.get('name') +
                        '_' + retries);
                      if (status.code !== grpc.status.OK) {
                        if (retries <= maxRetries) {
                          retry(message, metadata);
                        } else {
                          savedMessageNext(receivedMessage);
                          next(status);
                        }
                      } else {
                        registry.addCall('success_call');
                        var new_status = (new StatusBuilder())
                          .withCode(grpc.status.OK).build();
                        savedMessageNext(receivedMessage);
                        next(new_status);
                      }
                    }
                  });
                  newCall.sendMessage(message);
                  newCall.halfClose();
                };
                if (status.code !== grpc.status.OK) {
                  // Change the message we're sending only for test purposes
                  // so the server will respond without error
                  var newMessage = (savedMetadata.get('name')[0] === 'bar') ?
                    {value: 'bar'} : savedSendMessage;
                  retry(newMessage, savedMetadata);
                } else {
                  savedMessageNext(savedReceiveMessage);
                  next(status);
                }
              }
            ).build();
          next(metadata, new_listener);
        })
        .withSendMessage(function(message, next) {
          savedSendMessage = message;
          next(message);
        }).build();
      return new InterceptingCall(nextCall(options), requester);
    };

    var options = {
      interceptors: [retry_interceptor]
    };

    // Make a call which the server will return a non-OK status for
    var foo_message = {value: 'error'};
    var foo_metadata = new Metadata();
    foo_metadata.set('name', 'foo');
    client.echo(foo_message, foo_metadata, options, function(err, response) {
      assert.strictEqual(err.code, 2);
      registry.addCall('foo_result');
    });

    // Make a call which will fail the first time and succeed on the first
    // retry
    var bar_message = {value: 'error'};
    var bar_metadata = new Metadata();
    bar_metadata.set('name', 'bar');
    client.echo(bar_message, bar_metadata, options, function(err, response) {
      assert.strictEqual(response.value, 'bar');
      registry.addCall('bar_result');
    });
  });

  it('can retry and preserve interceptor order on success', function(done) {
    var registry = new CallRegistry(done,
      ['interceptor_c', 'retry_interceptor', 'fail_call', 'interceptor_c',
        'success_call', 'interceptor_a', 'result'], true);
    var interceptor_a = function(options, nextCall) {
      var requester = (new RequesterBuilder())
        .withStart(function(metadata, listener, next) {
          var new_listener = (new ListenerBuilder())
            .withOnReceiveMessage(function(message, next) {
              registry.addCall('interceptor_a');
              next(message);
            }).build();
          next(metadata, new_listener);
        }).build();
      return new InterceptingCall(nextCall(options), requester);
    };

    var retry_interceptor = function(options, nextCall) {
      var savedMetadata;
      var savedMessage;
      var savedMessageNext;
      var sendMessageNext;
      var originalMessage;
      var startNext;
      var originalListener;
      var requester = (new RequesterBuilder())
        .withStart(function(metadata, listener, next) {
          startNext = next;
          savedMetadata = metadata;
          originalListener = listener;
          var new_listener = (new ListenerBuilder())
            .withOnReceiveMessage(function(message, next) {
              savedMessage = message;
              savedMessageNext = next;
            })
            .withOnReceiveStatus(function(status, next) {
              var retries = 0;
              var maxRetries = 1;
              var receivedMessage;
              var retry = function(message, metadata) {
                retries++;
                var new_call = nextCall(options);
                new_call.start(metadata, {
                  onReceiveMessage: function(message) {
                    receivedMessage = message;
                  },
                  onReceiveStatus: function(status) {
                    if (status.code !== grpc.status.OK) {
                      if (retries <= maxRetries) {
                        retry(message, metadata);
                      } else {
                        savedMessageNext(receivedMessage);
                        next(status);
                      }
                    } else {
                      registry.addCall('success_call');
                      var new_status = (new StatusBuilder())
                        .withCode(grpc.status.OK).build();
                      savedMessageNext(receivedMessage);
                      next(new_status);
                    }
                  }
                });
                new_call.sendMessage(message);
                new_call.halfClose();
              };
              registry.addCall('retry_interceptor');
              if (status.code !== grpc.status.OK) {
                registry.addCall('fail_call');
                var newMessage = {value: 'foo'};
                retry(newMessage, savedMetadata);
              } else {
                savedMessageNext(savedMessage);
                next(status);
              }
            }).build();
          next(metadata, new_listener);
        })
        .withSendMessage(function(message, next) {
          sendMessageNext = next;
          originalMessage = message;
          next(message);
        })
        .build();
      return new InterceptingCall(nextCall(options), requester);
    };

    var interceptor_c = function(options, nextCall) {
      var requester = (new RequesterBuilder())
        .withStart(function(metadata, listener, next) {
          var new_listener = (new ListenerBuilder())
            .withOnReceiveMessage(function(message, next) {
              registry.addCall('interceptor_c');
              next(message);
            }).build();
          next(metadata, new_listener);
        }).build();
      return new InterceptingCall(nextCall(options), requester);
    };

    var options = {
      interceptors: [interceptor_a, retry_interceptor, interceptor_c]
    };

    var message = {value: 'error'};
    client.echo(message, options, function(err, response) {
      assert.strictEqual(response.value, 'foo');
      registry.addCall('result');
    });
  });

  describe('handle interceptor errors', function() {
    var foo_interceptor = function(options, nextCall) {
      var savedListener;
      var outbound = (new RequesterBuilder())
        .withStart(function(metadata, listener, next) {
          savedListener = listener;
          next(metadata, listener);
        })
        .withSendMessage(function(message, next) {
          savedListener.onReceiveMetadata(new Metadata());
          savedListener.onReceiveMessage({value: 'failed'});
          var error_status = (new StatusBuilder())
            .withCode(16)
            .withDetails('Error in foo interceptor')
            .build();
          savedListener.onReceiveStatus(error_status);
        }).build();
      return new grpc_client.InterceptingCall(nextCall(options), outbound);
    };
    var options = { interceptors: [foo_interceptor] };

    it('with unary call', function(done) {
      var message = {};
      client.echo(message, options, function(err, response) {
        assert.strictEqual(err.code, 16);
        assert.strictEqual(err.message, 'Error in foo interceptor');
        done();
      });
    });
  });

  describe('implement fallbacks for streaming RPCs', function() {

    var fallback_response = {value: 'fallback'};
    var savedMessage;
    var savedMessageNext;
    var interceptor = function(options, nextCall) {
      var requester = (new RequesterBuilder())
        .withStart(function(metadata, listener, next) {
          var new_listener = (new ListenerBuilder())
            .withOnReceiveMessage(function(message, next) {
              savedMessage = message;
              savedMessageNext = next;
            })
            .withOnReceiveStatus(function(status, next) {
              if (status.code !== grpc.status.OK) {
                savedMessageNext(fallback_response);
                next((new StatusBuilder()).withCode(grpc.status.OK));
              } else {
                savedMessageNext(savedMessage);
                next(status);
              }
            }).build();
          next(metadata, new_listener);
        }).build();
      return new InterceptingCall(nextCall(options), requester);
    };
    var options = {
      interceptors: [interceptor]
    };
    it('with client streaming call', function(done) {
      var registry = new CallRegistry(done, ['foo_result', 'fallback_result']);
      var stream = client.echoClientStream(options, function (err, response) {
        assert.strictEqual(response.value, 'foo');
        registry.addCall('foo_result');
      });
      stream.write({value: 'foo'});
      stream.end();

      stream = client.echoClientStream(options, function(err, response) {
        assert.strictEqual(response.value, 'fallback');
        registry.addCall('fallback_result');
      });
      stream.write({value: 'error'});
      stream.end();
    });
  });

  describe('allows the call options to be modified for downstream interceptors',
    function() {
      var done;
      var method_name;
      var method_path_last;
      var interceptor_a = function(options, nextCall) {
        options.deadline = 10;
        return new InterceptingCall(nextCall(options));
      };
      var interceptor_b = function(options, nextCall) {
        assert.equal(options.method_descriptor.path, '/EchoService/' +
          method_path_last);
        assert.equal(options.method_descriptor.name, method_name);
        assert.equal(options.deadline, 10);
        done();
        return new InterceptingCall(nextCall(options));
      };

      var options = {
        interceptors: [interceptor_a, interceptor_b],
        deadline: 100
      };

      it('with unary call', function(cb) {
        done = cb;
        var metadata = new Metadata();
        var message = {};
        method_name = 'echo';
        method_path_last = 'Echo';

        client.echo(message, metadata, options, function(){});
      });

      it('with client streaming call', function(cb) {
        done = cb;
        var metadata = new Metadata();
        method_name = 'echoClientStream';
        method_path_last = 'EchoClientStream';

        client.echoClientStream(metadata, options, function() {});
      });

      it('with server streaming call', function(cb) {
        done = cb;
        var metadata = new Metadata();
        var message = {};
        method_name = 'echoServerStream';
        method_path_last = 'EchoServerStream';

        client.echoServerStream(message, metadata, options);
      });

      it('with bidi streaming call', function(cb) {
        done = cb;
        var metadata = new Metadata();
        method_name = 'echoBidiStream';
        method_path_last = 'EchoBidiStream';

        client.echoBidiStream(metadata, options);
      });
    });

  describe('pass accurate MethodDescriptors', function() {
    var registry;
    var initial_value = 'broken';
    var expected_value = 'working';
    var interceptor = function(options, nextCall) {
      registry.addCall({
        name: options.method_descriptor.name,
        type: options.method_descriptor.method_type
      });
      var outbound = (new RequesterBuilder())
        .withSendMessage(function(message, next) {
          message.value = expected_value;
          next(message);
        }).build();
      return new InterceptingCall(nextCall(options), outbound);
    };

    var options = { interceptors: [interceptor] };

    it('with unary call', function(done) {
      var unary_descriptor = {
        name: 'echo',
        type: grpc_client.MethodType.UNARY
      };
      registry = new CallRegistry(done, [
        unary_descriptor,
        'result_unary'
      ]);

      var metadata = new Metadata();

      var message = {value: initial_value};

      client.echo(message, metadata, options, function(err, response){
        assert.equal(response.value, expected_value);
        registry.addCall('result_unary');
      });

    });
    it('with client streaming call', function(done) {

      var client_stream_descriptor = {
        name: 'echoClientStream',
        type: grpc_client.MethodType.CLIENT_STREAMING
      };
      registry = new CallRegistry(done, [
        client_stream_descriptor,
        'result_client_stream'
      ]);
      var metadata = new Metadata();
      var message = {value: initial_value};
      var client_stream = client.echoClientStream(metadata, options,
        function(err, response) {
          assert.strictEqual(response.value, expected_value);
          registry.addCall('result_client_stream');
        });
      client_stream.write(message);
      client_stream.end();

    });
    it('with server streaming call', function(done) {
      var server_stream_descriptor = {
        name: 'echoServerStream',
        type: grpc_client.MethodType.SERVER_STREAMING
      };
      registry = new CallRegistry(done, [
        server_stream_descriptor,
        'result_server_stream'
      ]);

      var metadata = new Metadata();
      var message = {value: initial_value};
      var server_stream = client.echoServerStream(message, metadata, options);
      server_stream.on('data', function(data) {
        assert.strictEqual(data.value, expected_value);
        registry.addCall('result_server_stream');
      });

    });
    it('with bidi streaming call', function(done) {
      var bidi_stream_descriptor = {
        name: 'echoBidiStream',
        type: grpc_client.MethodType.BIDI_STREAMING
      };
      registry = new CallRegistry(done, [
        bidi_stream_descriptor,
        'result_bidi_stream'
      ]);

      var metadata = new Metadata();
      var message = {value: initial_value};
      var bidi_stream = client.echoBidiStream(metadata, options);
      bidi_stream.on('data', function(data) {
        assert.strictEqual(data.value, expected_value);
        registry.addCall('result_bidi_stream');
      });
      bidi_stream.write(message);
      bidi_stream.end();
    });
  });

  it('uses interceptors passed to the client constructor', function(done) {
    var registry = new CallRegistry(done, {
      'constructor_interceptor_a_echo': 1,
      'constructor_interceptor_b_echoServerStream': 1,
      'invocation_interceptor': 1,
      'result_unary': 1,
      'result_stream': 1,
      'result_invocation': 1
    });

    var constructor_interceptor_a = function(options, nextCall) {
      var outbound = (new RequesterBuilder())
        .withStart(function(metadata, listener, next) {
          registry.addCall('constructor_interceptor_a_' +
            options.method_descriptor.name);
          next(metadata, listener);
        }).build();
      return new InterceptingCall(nextCall(options), outbound);
    };
    var constructor_interceptor_b = function(options, nextCall) {
      var outbound = (new RequesterBuilder())
        .withStart(function(metadata, listener, next) {
          registry.addCall('constructor_interceptor_b_' +
            options.method_descriptor.name);
          next(metadata, listener);
        }).build();
      return new InterceptingCall(nextCall(options), outbound);
    };
    var invocation_interceptor = function(options, nextCall) {
      var outbound = (new RequesterBuilder())
        .withStart(function(metadata, listener, next) {
          registry.addCall('invocation_interceptor');
          next(metadata, listener);
        }).build();
      return new InterceptingCall(nextCall(options), outbound);
    };

    var interceptor_providers = [
      new InterceptorProvider(function(method_descriptor) {
        if (method_descriptor.method_type === grpc_client.MethodType.UNARY) {
          return constructor_interceptor_a;
        }
      }),
      new InterceptorProvider(function(method_descriptor) {
        var t = method_descriptor.method_type;
        if (t === grpc_client.MethodType.SERVER_STREAMING) {
          return constructor_interceptor_b;
        }
      })
    ];
    var constructor_options = {
      interceptor_providers: interceptor_providers
    };
    var IntClient = grpc_client.makeClientConstructor(echo_service);
    var int_client = new IntClient('localhost:' + echo_port, insecureCreds,
      constructor_options);
    var message = {};
    int_client.echo(message, function() {
      registry.addCall('result_unary');
    });
    var stream = int_client.echoServerStream(message);
    stream.on('data', function() {
      registry.addCall('result_stream');
    });

    var options = { interceptors: [invocation_interceptor] };
    int_client.echo(message, options, function() {
      registry.addCall('result_invocation');
    });

    assert(_.isEqual(grpc_client.getClientInterceptors(int_client), {
      echo: [constructor_interceptor_a],
      echoClientStream: [],
      echoServerStream: [constructor_interceptor_b],
      echoBidiStream: []
    }));
  });

  it('will reject conflicting interceptor options at invocation',
    function(done) {
      try {
        client.echo('message', {
          interceptors: [],
          interceptor_providers: []
        }, function () {});
      } catch (e) {
        assert.equal(e.name, 'InterceptorConfigurationError');
        done();
      }
    });

  it('will resolve interceptor providers at invocation', function(done) {
    var constructor_interceptor = function(options, nextCall) {
      var outbound = (new RequesterBuilder())
        .withStart(function() {
          assert(false);
        }).build();
      return new InterceptingCall(nextCall(options), outbound);
    };
    var invocation_interceptor = function(options, nextCall) {
      var outbound = (new RequesterBuilder())
        .withStart(function() {
          done();
        }).build();
      return new InterceptingCall(nextCall(options), outbound);
    };
    var constructor_interceptor_providers = [
      new InterceptorProvider(function() {
        return constructor_interceptor;
      })
    ];
    var invocation_interceptor_providers = [
      new InterceptorProvider(function() {
        return invocation_interceptor;
      })
    ];
    var constructor_options = {
      interceptor_providers: constructor_interceptor_providers
    };
    var IntClient = grpc_client.makeClientConstructor(echo_service);
    var int_client = new IntClient('localhost:' + echo_port, insecureCreds,
      constructor_options);
    var message = {};
    var options = { interceptor_providers: invocation_interceptor_providers };
    int_client.echo(message, options, function() {});
  });

  describe('trigger a stack of interceptors in nested order', function() {
    var registry;
    var expected_calls = ['constructA', 'constructB', 'outboundA', 'outboundB',
      'inboundB', 'inboundA'];
    var interceptor_a = function(options, nextCall) {
      registry.addCall('constructA');
      var outbound = (new RequesterBuilder())
        .withStart(function(metadata, listener, next) {
          registry.addCall('outboundA');
          var new_listener = (new ListenerBuilder()).withOnReceiveMessage(
            function(message, next) {
              registry.addCall('inboundA');
              next(message);
            }).build();
          next(metadata, new_listener);
        }).build();
      return new grpc_client.InterceptingCall(nextCall(options),
        outbound);
    };
    var interceptor_b = function(options, nextCall) {
      registry.addCall('constructB');
      var outbound = (new RequesterBuilder())
        .withStart(function(metadata, listener, next) {
          registry.addCall('outboundB');
          var new_listener = (new ListenerBuilder()).withOnReceiveMessage(
            function(message, next) {
              registry.addCall('inboundB');
              next(message);
            }).build();
          next(metadata, new_listener);
        }).build();
      return new grpc_client.InterceptingCall(nextCall(options),
        outbound);
    };
    var options = { interceptors: [interceptor_a, interceptor_b] };
    var metadata = new Metadata();
    var message = {};

    it('with unary call', function(done) {
      registry = new CallRegistry(done, expected_calls, true);
      client.echo(message, metadata, options, function(){});
    });

    it('with client streaming call', function(done) {
      registry = new CallRegistry(done, expected_calls, true);
      var client_stream = client.echoClientStream(metadata, options,
        function() {});
      client_stream.write(message);
      client_stream.end();
    });

    it('with server streaming call', function(done) {
      registry = new CallRegistry(done, expected_calls, true);
      var stream = client.echoServerStream(message, metadata, options);
      stream.on('data', function() {});
    });

    it('with bidi streaming call', function(done) {
      registry = new CallRegistry(done, expected_calls, true);
      var bidi_stream = client.echoBidiStream(metadata, options);
      bidi_stream.on('data', function(){});
      bidi_stream.write(message);
      bidi_stream.end();
    });
  });

  describe('trigger interceptors horizontally', function() {
    var expected_calls = [
      'interceptor_a_start',
      'interceptor_b_start',
      'interceptor_a_send',
      'interceptor_b_send'
    ];
    var registry;
    var interceptor_a = function(options, nextCall) {
      var outbound = (new RequesterBuilder())
        .withStart(function(metadata, listener, next) {
          registry.addCall('interceptor_a_start');
          next(metadata, listener);
        })
        .withSendMessage(function(message, next) {
          registry.addCall('interceptor_a_send');
          next(message);
        }).build();
      return new grpc_client.InterceptingCall(nextCall(options),
        outbound);
    };
    var interceptor_b = function(options, nextCall) {
      var outbound = (new RequesterBuilder())
        .withStart(function(metadata, listener, next) {
          registry.addCall('interceptor_b_start');
          next(metadata, listener);
        })
        .withSendMessage(function(message, next) {
          registry.addCall('interceptor_b_send');
          next(message);
        }).build();
      return new grpc_client.InterceptingCall(nextCall(options),
        outbound);
    };

    var options = { interceptors: [interceptor_a, interceptor_b] };
    var metadata = new Metadata();
    var message = {};

    it('with unary call', function(done) {
      registry = new CallRegistry(done, expected_calls, true);
      client.echo(message, metadata, options, function(){});
    });

    it('with client streaming call', function(done) {
      registry = new CallRegistry(done, expected_calls, true);
      var client_stream = client.echoClientStream(metadata, options,
        function() {});
      client_stream.write(message);
      client_stream.end();
    });

    it('with server streaming call', function(done) {
      registry = new CallRegistry(done, expected_calls, true);
      var stream = client.echoServerStream(message, metadata, options);
      stream.on('data', function() {});
    });

    it('with bidi streaming call', function(done) {
      registry = new CallRegistry(done, expected_calls, true);
      var bidi_stream = client.echoBidiStream(metadata, options);
      bidi_stream.on('data', function(){});
      bidi_stream.write(message);
      bidi_stream.end();
    });
  });

  describe('trigger when sending metadata', function() {
    var registry;

    var message = {};
    var key_names = ['original', 'foo', 'bar'];
    var keys = {
      original: 'originalkey',
      foo: 'fookey',
      bar: 'barkey'
    };
    var values = {
      original: 'originalvalue',
      foo: 'foovalue',
      bar: 'barvalue'
    };
    var expected_calls = ['foo', 'bar', 'response'];
    var foo_interceptor = function(options, nextCall) {
      var outbound = (new RequesterBuilder())
        .withStart(function(metadata, listener, next) {
          metadata.add(keys.foo, values.foo);
          registry.addCall('foo');
          next(metadata, listener);
        }).build();
      return new InterceptingCall(nextCall(options), outbound);
    };
    var bar_interceptor = function(options, nextCall) {
      var outbound = (new RequesterBuilder())
        .withStart(function(metadata, listener, next) {
          metadata.add(keys.bar, values.bar);
          registry.addCall('bar');
          next(metadata, listener);
        }).build();
      return new InterceptingCall(nextCall(options), outbound);
    };
    var options = { interceptors: [foo_interceptor, bar_interceptor] };

    it('with unary call', function(done) {
      registry = new CallRegistry(done, expected_calls, true);
      var metadata = new Metadata();
      metadata.add(keys.original, values.original);

      var unary_call = client.echo(message, metadata, options, function () {});
      unary_call.on('metadata', function (metadata) {
        var has_expected_values = _.every(key_names, function (key_name) {
          return _.isEqual(metadata.get(keys[key_name]), [values[key_name]]);
        });
        assert(has_expected_values);
        registry.addCall('response');
      });
    });
    it('with client streaming call', function(done) {
      registry = new CallRegistry(done, expected_calls, true);
      var metadata = new Metadata();
      metadata.add(keys.original, values.original);

      var client_stream = client.echoClientStream(metadata, options,
        function () {
        });
      client_stream.write(message);
      client_stream.on('metadata', function (metadata) {
        var has_expected_values = _.every(key_names, function (key_name) {
          return _.isEqual(metadata.get(keys[key_name]), [values[key_name]]);
        });
        assert(has_expected_values);
        registry.addCall('response');
      });
      client_stream.end();
    });
    it('with server streaming call', function(done) {
      registry = new CallRegistry(done, expected_calls, true);
      var metadata = new Metadata();
      metadata.add(keys.original, values.original);
      var server_stream = client.echoServerStream(message, metadata, options);
      server_stream.on('metadata', function (metadata) {
        var has_expected_values = _.every(key_names, function (key_name) {
          return _.isEqual(metadata.get(keys[key_name]), [values[key_name]]);
        });
        assert(has_expected_values);
        registry.addCall('response');
      });
      server_stream.on('data', function() { });
    });
    it('with bidi streaming call', function(done) {
      registry = new CallRegistry(done, expected_calls, true);
      var metadata = new Metadata();
      metadata.add(keys.original, values.original);
      var bidi_stream = client.echoBidiStream(metadata, options);
      bidi_stream.on('metadata', function(metadata) {
        var has_expected_values = _.every(key_names, function(key_name) {
          return _.isEqual(metadata.get(keys[key_name]),[values[key_name]]);
        });
        assert(has_expected_values);
        bidi_stream.end();
        registry.addCall('response');
      });
      bidi_stream.on('data', function() { });
      bidi_stream.write(message);
      bidi_stream.end();
    });
  });

  describe('trigger when sending messages', function() {
    var registry;
    var originalValue = 'foo';
    var expectedValue = 'bar';
    var foo_interceptor = function(options, nextCall) {
      var outbound = (new RequesterBuilder())
        .withSendMessage(function(message, next) {
          assert.strictEqual(message.value, originalValue);
          registry.addCall('messageIntercepted');
          next({value: expectedValue});
        }).build();
      return new grpc_client.InterceptingCall(nextCall(options),
        outbound);
    };
    var expected_calls = ['messageIntercepted', 'response'];
    var options = { interceptors: [foo_interceptor] };
    var metadata = new Metadata();

    it('with unary call', function(done) {
      registry = new CallRegistry(done, expected_calls, true);
      var message = {value: originalValue};

      client.echo(message, metadata, options, function (err, response) {
        assert.strictEqual(response.value, expectedValue);
        registry.addCall('response');
      });
    });
    it('with client streaming call', function(done) {
      registry = new CallRegistry(done, expected_calls, true);
      var message = {value: originalValue};
      var client_stream = client.echoClientStream(metadata, options,
        function (err, response) {
          assert.strictEqual(response.value, expectedValue);
          registry.addCall('response');
        });
      client_stream.write(message);
      client_stream.end();
    });
    it('with server streaming call', function(done) {
      registry = new CallRegistry(done, expected_calls, true);
      var message = {value: originalValue};
      var server_stream = client.echoServerStream(message, metadata, options);
      server_stream.on('data', function (data) {
        assert.strictEqual(data.value, expectedValue);
        registry.addCall('response');
      });
    });
    it('with bidi streaming call', function(done) {
      registry = new CallRegistry(done, expected_calls, true);
      var message = {value: originalValue};
      var bidi_stream = client.echoBidiStream(metadata, options);
      bidi_stream.on('data', function(data) {
        assert.strictEqual(data.value, expectedValue);
        registry.addCall('response');
      });
      bidi_stream.write(message);
      bidi_stream.end();
    });
  });

  describe('trigger when client closes the call', function() {
    var registry;
    var expected_calls = [
      'response', 'halfClose'
    ];
    var message = {};
    var foo_interceptor = function(options, nextCall) {
      var outbound = (new RequesterBuilder())
        .withHalfClose(function(next) {
          registry.addCall('halfClose');
          next();
        }).build();
      return new grpc_client.InterceptingCall(nextCall(options),
        outbound);
    };
    var options = { interceptors: [foo_interceptor] };
    it('with unary call', function(done) {
      registry = new CallRegistry(done, expected_calls);
      client.echo(message, options, function (err, response) {
        if (!err) {
          registry.addCall('response');
        }
      });
    });
    it('with client streaming call', function(done) {
      registry = new CallRegistry(done, expected_calls);
      var client_stream = client.echoClientStream(options,
        function(err, response) {});
      client_stream.write(message, function(err) {
        if (!err) {
          registry.addCall('response');
        }
      });
      client_stream.end();
    });
    it('with server streaming call', function(done) {
      registry = new CallRegistry(done, expected_calls);
      var server_stream = client.echoServerStream(message, options);
      server_stream.on('data', function (data) {
        registry.addCall('response');
      });
    });
    it('with bidi streaming call', function(done) {
      registry = new CallRegistry(done, expected_calls);
      var bidi_stream = client.echoBidiStream(options);
      bidi_stream.on('data', function(data) {
        registry.addCall('response');
      });
      bidi_stream.write(message);
      bidi_stream.end();
    });
  });

  describe('trigger when the stream is canceled', function() {
    var done;
    var message = {};
    var foo_interceptor = function(options, nextCall) {
      var outbound = (new RequesterBuilder())
        .withCancel(function(next) {
          done();
          next();
        }).build();
      return new grpc_client.InterceptingCall(nextCall(options),
        outbound);
    };
    var options = { interceptors: [foo_interceptor] };

    it('with unary call', function(cb) {
      done = cb;
      var stream = client.echo(message, options, function() {});
      stream.cancel();
    });

    it('with client streaming call', function(cb) {
      done = cb;
      var stream = client.echoClientStream(options, function() {});
      stream.cancel();
    });
    it('with server streaming call', function(cb) {
      done = cb;
      var stream = client.echoServerStream(message, options);
      stream.cancel();
    });
    it('with bidi streaming call', function(cb) {
      done = cb;
      var stream = client.echoBidiStream(options);
      stream.cancel();
    });
  });

  describe('trigger when receiving metadata', function() {
    var message = {};
    var expectedKey = 'foo';
    var expectedValue = 'bar';
    var foo_interceptor = function(options, nextCall) {
      var outbound = (new RequesterBuilder())
        .withStart(function(metadata, listener, next) {
          var new_listener = (new ListenerBuilder()).withOnReceiveMetadata(
            function(metadata, next) {
              metadata.add(expectedKey, expectedValue);
              next(metadata);
            }).build();
          next(metadata, new_listener);
        }).build();
      return new grpc_client.InterceptingCall(nextCall(options), outbound);
    };
    var options = { interceptors: [foo_interceptor] };
    it('with unary call', function(done) {
      var metadata = new Metadata();
      var unary_call = client.echo(message, metadata, options, function () {});
      unary_call.on('metadata', function (metadata) {
        assert.strictEqual(metadata.get(expectedKey)[0], expectedValue);
        done();
      });
    });
    it('with client streaming call', function(done) {
      var metadata = new Metadata();
      var client_stream = client.echoClientStream(metadata, options,
        function () {});
      client_stream.write(message);
      client_stream.on('metadata', function (metadata) {
        assert.strictEqual(metadata.get(expectedKey)[0], expectedValue);
        done();
      });
      client_stream.end();
    });
    it('with server streaming call', function(done) {
      var metadata = new Metadata();
      var server_stream = client.echoServerStream(message, metadata, options);
      server_stream.on('metadata', function (metadata) {
        assert.strictEqual(metadata.get(expectedKey)[0], expectedValue);
        done();
      });
      server_stream.on('data', function() { });
    });
    it('with bidi streaming call', function(done) {
      var metadata = new Metadata();
      var bidi_stream = client.echoBidiStream(metadata, options);
      bidi_stream.on('metadata', function(metadata) {
        assert.strictEqual(metadata.get(expectedKey)[0], expectedValue);
        bidi_stream.end();
        done();
      });
      bidi_stream.on('data', function() { });
      bidi_stream.write(message);
    });
  });

  describe('trigger when sending messages', function() {
    var originalValue = 'foo';
    var expectedValue = 'bar';
    var foo_interceptor = function(options, nextCall) {
      var outbound = (new RequesterBuilder())
        .withStart(function(metadata, listener, next) {
          var new_listener = (new ListenerBuilder()).withOnReceiveMessage(
            function(message, next) {
              if (!message) {
                next(message);
                return;
              }
              assert.strictEqual(message.value, originalValue);
              message.value = expectedValue;
              next(message);
            }).build();
          next(metadata, new_listener);
        }).build();
      return new grpc_client.InterceptingCall(nextCall(options),
        outbound);
    };
    var options = { interceptors: [foo_interceptor] };
    var metadata = new Metadata();
    it('with unary call', function(done) {
      var message = {value: originalValue};
      client.echo(message, metadata, options, function (err, response) {
        assert.strictEqual(response.value, expectedValue);
        done();
      });
    });
    it('with client streaming call', function(done) {
      var message = {value: originalValue};
      var client_stream = client.echoClientStream(metadata, options,
        function(err, response) {
          assert.strictEqual(response.value, expectedValue);
          done();
        });
      client_stream.write(message);
      client_stream.end();
    });
    it('with server streaming call', function(done) {
      var message = {value: originalValue};
      var server_stream = client.echoServerStream(message, metadata, options);
      server_stream.on('data', function (data) {
        assert.strictEqual(data.value, expectedValue);
        done();
      });
    });
    it('with bidi streaming call', function(done) {
      var message = {value: originalValue};
      var bidi_stream = client.echoBidiStream(metadata, options);
      bidi_stream.on('data', function(data) {
        assert.strictEqual(data.value, expectedValue);
        done();
      });
      bidi_stream.write(message);
      bidi_stream.end();
    });
  });

  describe('trigger when receiving status', function() {
    var expectedStatus = 'foo';
    var foo_interceptor = function(options, nextCall) {
      var outbound = (new RequesterBuilder())
        .withStart(function(metadata, listener, next) {
          var new_listener = (new ListenerBuilder()).withOnReceiveStatus(
            function(status, next) {
              assert.strictEqual(status.code, 2);
              assert.strictEqual(status.details, 'test status message');
              var new_status = {
                code: 1,
                details: expectedStatus,
                metadata: {}
              };
              next(new_status);
            }).build();
          next(metadata, new_listener);
        }).build();
      return new grpc_client.InterceptingCall(nextCall(options),
        outbound);
    };
    var options = { interceptors: [foo_interceptor] };
    var metadata = new Metadata();
    it('with unary call', function(done) {
      var message = {value: 'error'};
      var unary_call = client.echo(message, metadata, options, function () {
      });
      unary_call.on('status', function (status) {
        assert.strictEqual(status.code, 1);
        assert.strictEqual(status.details, expectedStatus);
        done();
      });
    });
    it('with client streaming call', function(done) {
      var message = {value: 'error'};
      var client_stream = client.echoClientStream(metadata, options,
        function () {
        });
      client_stream.on('status', function (status) {
        assert.strictEqual(status.code, 1);
        assert.strictEqual(status.details, expectedStatus);
        done();
      });
      client_stream.write(message);
      client_stream.end();
    });

    it('with server streaming call', function(done) {
      var message = {value: 'error'};
      var server_stream = client.echoServerStream(message, metadata, options);
      server_stream.on('error', function (err) {
      });
      server_stream.on('data', function (data) {
      });
      server_stream.on('status', function (status) {
        assert.strictEqual(status.code, 1);
        assert.strictEqual(status.details, expectedStatus);
        done();
      });
    });

    it('with bidi streaming call', function(done) {
      var message = {value: 'error'};
      var bidi_stream = client.echoBidiStream(metadata, options);
      bidi_stream.on('error', function(err) {});
      bidi_stream.on('data', function(data) {});
      bidi_stream.on('status', function(status) {
        assert.strictEqual(status.code, 1);
        assert.strictEqual(status.details, expectedStatus);
        done();
      });
      bidi_stream.write(message);
      bidi_stream.end();
    });
  });
});
