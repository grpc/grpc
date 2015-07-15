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

var grpc = require('bindings')('grpc.node');

var common = require('./common');

var stream = require('stream');

var Readable = stream.Readable;
var Writable = stream.Writable;
var Duplex = stream.Duplex;
var util = require('util');

var EventEmitter = require('events').EventEmitter;

/**
 * Handle an error on a call by sending it as a status
 * @param {grpc.Call} call The call to send the error on
 * @param {Object} error The error object
 */
function handleError(call, error) {
  var status = {
    code: grpc.status.UNKNOWN,
    details: 'Unknown Error',
    metadata: {}
  };
  if (error.hasOwnProperty('message')) {
    status.details = error.message;
  }
  if (error.hasOwnProperty('code')) {
    status.code = error.code;
    if (error.hasOwnProperty('details')) {
      status.details = error.details;
    }
  }
  if (error.hasOwnProperty('metadata')) {
    status.metadata = error.metadata;
  }
  var error_batch = {};
  error_batch[grpc.opType.SEND_STATUS_FROM_SERVER] = status;
  call.startBatch(error_batch, function(){});
}

/**
 * Wait for the client to close, then emit a cancelled event if the client
 * cancelled.
 * @param {grpc.Call} call The call object to wait on
 * @param {EventEmitter} emitter The event emitter to emit the cancelled event
 *     on
 */
function waitForCancel(call, emitter) {
  var cancel_batch = {};
  cancel_batch[grpc.opType.RECV_CLOSE_ON_SERVER] = true;
  call.startBatch(cancel_batch, function(err, result) {
    if (err) {
      emitter.emit('error', err);
    }
    if (result.cancelled) {
      emitter.cancelled = true;
      emitter.emit('cancelled');
    }
  });
}

/**
 * Send a response to a unary or client streaming call.
 * @param {grpc.Call} call The call to respond on
 * @param {*} value The value to respond with
 * @param {function(*):Buffer=} serialize Serialization function for the
 *     response
 * @param {Object=} metadata Optional trailing metadata to send with status
 */
function sendUnaryResponse(call, value, serialize, metadata) {
  var end_batch = {};
  var status = {
    code: grpc.status.OK,
    details: 'OK',
    metadata: {}
  };
  if (metadata) {
    status.metadata = metadata;
  }
  end_batch[grpc.opType.SEND_MESSAGE] = serialize(value);
  end_batch[grpc.opType.SEND_STATUS_FROM_SERVER] = status;
  call.startBatch(end_batch, function (){});
}

/**
 * Initialize a writable stream. This is used for both the writable and duplex
 * stream constructors.
 * @param {Writable} stream The stream to set up
 * @param {function(*):Buffer=} Serialization function for responses
 */
function setUpWritable(stream, serialize) {
  stream.finished = false;
  stream.status = {
    code : grpc.status.OK,
    details : 'OK',
    metadata : {}
  };
  stream.serialize = common.wrapIgnoreNull(serialize);
  function sendStatus() {
    var batch = {};
    batch[grpc.opType.SEND_STATUS_FROM_SERVER] = stream.status;
    stream.call.startBatch(batch, function(){});
  }
  stream.on('finish', sendStatus);
  /**
   * Set the pending status to a given error status. If the error does not have
   * code or details properties, the code will be set to grpc.status.UNKNOWN
   * and the details will be set to 'Unknown Error'.
   * @param {Error} err The error object
   */
  function setStatus(err) {
    var code = grpc.status.UNKNOWN;
    var details = 'Unknown Error';
    var metadata = {};
    if (err.hasOwnProperty('message')) {
      details = err.message;
    }
    if (err.hasOwnProperty('code')) {
      code = err.code;
      if (err.hasOwnProperty('details')) {
        details = err.details;
      }
    }
    if (err.hasOwnProperty('metadata')) {
      metadata = err.metadata;
    }
    stream.status = {code: code, details: details, metadata: metadata};
  }
  /**
   * Terminate the call. This includes indicating that reads are done, draining
   * all pending writes, and sending the given error as a status
   * @param {Error} err The error object
   * @this GrpcServerStream
   */
  function terminateCall(err) {
    // Drain readable data
    setStatus(err);
    stream.end();
  }
  stream.on('error', terminateCall);
  /**
   * Override of Writable#end method that allows for sending metadata with a
   * success status.
   * @param {Object=} metadata Metadata to send with the status
   */
  stream.end = function(metadata) {
    if (metadata) {
      stream.status.metadata = metadata;
    }
    Writable.prototype.end.call(this);
  };
}

/**
 * Initialize a readable stream. This is used for both the readable and duplex
 * stream constructors.
 * @param {Readable} stream The stream to initialize
 * @param {function(Buffer):*=} deserialize Deserialization function for
 *     incoming data.
 */
function setUpReadable(stream, deserialize) {
  stream.deserialize = common.wrapIgnoreNull(deserialize);
  stream.finished = false;
  stream.reading = false;

  stream.terminate = function() {
    stream.finished = true;
    stream.on('data', function() {});
  };

  stream.on('cancelled', function() {
    stream.terminate();
  });
}

util.inherits(ServerWritableStream, Writable);

/**
 * A stream that the server can write to. Used for calls that are streaming from
 * the server side.
 * @constructor
 * @param {grpc.Call} call The call object to send data with
 * @param {function(*):Buffer=} serialize Serialization function for writes
 */
function ServerWritableStream(call, serialize) {
  Writable.call(this, {objectMode: true});
  this.call = call;

  this.finished = false;
  setUpWritable(this, serialize);
}

/**
 * Start writing a chunk of data. This is an implementation of a method required
 * for implementing stream.Writable.
 * @param {Buffer} chunk The chunk of data to write
 * @param {string} encoding Ignored
 * @param {function(Error=)} callback Callback to indicate that the write is
 *     complete
 */
function _write(chunk, encoding, callback) {
  /* jshint validthis: true */
  var batch = {};
  batch[grpc.opType.SEND_MESSAGE] = this.serialize(chunk);
  this.call.startBatch(batch, function(err, value) {
    if (err) {
      this.emit('error', err);
      return;
    }
    callback();
  });
}

ServerWritableStream.prototype._write = _write;

util.inherits(ServerReadableStream, Readable);

/**
 * A stream that the server can read from. Used for calls that are streaming
 * from the client side.
 * @constructor
 * @param {grpc.Call} call The call object to read data with
 * @param {function(Buffer):*=} deserialize Deserialization function for reads
 */
function ServerReadableStream(call, deserialize) {
  Readable.call(this, {objectMode: true});
  this.call = call;
  setUpReadable(this, deserialize);
}

/**
 * Start reading from the gRPC data source. This is an implementation of a
 * method required for implementing stream.Readable
 * @param {number} size Ignored
 */
function _read(size) {
  /* jshint validthis: true */
  var self = this;
  /**
   * Callback to be called when a READ event is received. Pushes the data onto
   * the read queue and starts reading again if applicable
   * @param {grpc.Event} event READ event object
   */
  function readCallback(err, event) {
    if (err) {
      self.terminate();
      return;
    }
    if (self.finished) {
      self.push(null);
      return;
    }
    var data = event.read;
    var deserialized;
    try {
      deserialized = self.deserialize(data);
    } catch (e) {
      e.code = grpc.status.INVALID_ARGUMENT;
      self.emit('error', e);
      return;
    }
    if (self.push(deserialized) && data !== null) {
      var read_batch = {};
      read_batch[grpc.opType.RECV_MESSAGE] = true;
      self.call.startBatch(read_batch, readCallback);
    } else {
      self.reading = false;
    }
  }
  if (self.finished) {
    self.push(null);
  } else {
    if (!self.reading) {
      self.reading = true;
      var batch = {};
      batch[grpc.opType.RECV_MESSAGE] = true;
      self.call.startBatch(batch, readCallback);
    }
  }
}

ServerReadableStream.prototype._read = _read;

util.inherits(ServerDuplexStream, Duplex);

/**
 * A stream that the server can read from or write to. Used for calls with
 * duplex streaming.
 * @constructor
 * @param {grpc.Call} call Call object to proxy
 * @param {function(*):Buffer=} serialize Serialization function for requests
 * @param {function(Buffer):*=} deserialize Deserialization function for
 *     responses
 */
function ServerDuplexStream(call, serialize, deserialize) {
  Duplex.call(this, {objectMode: true});
  this.call = call;
  setUpWritable(this, serialize);
  setUpReadable(this, deserialize);
}

ServerDuplexStream.prototype._read = _read;
ServerDuplexStream.prototype._write = _write;

/**
 * Fully handle a unary call
 * @param {grpc.Call} call The call to handle
 * @param {Object} handler Request handler object for the method that was called
 * @param {Object} metadata Metadata from the client
 */
function handleUnary(call, handler, metadata) {
  var emitter = new EventEmitter();
  emitter.on('error', function(error) {
    handleError(call, error);
  });
  waitForCancel(call, emitter);
  var batch = {};
  batch[grpc.opType.SEND_INITIAL_METADATA] = metadata;
  batch[grpc.opType.RECV_MESSAGE] = true;
  call.startBatch(batch, function(err, result) {
    if (err) {
      handleError(call, err);
      return;
    }
    try {
      emitter.request = handler.deserialize(result.read);
    } catch (e) {
      e.code = grpc.status.INVALID_ARGUMENT;
      handleError(call, e);
      return;
    }
    if (emitter.cancelled) {
      return;
    }
    handler.func(emitter, function sendUnaryData(err, value, trailer) {
      if (err) {
        if (trailer) {
          err.metadata = trailer;
        }
        handleError(call, err);
      } else {
        sendUnaryResponse(call, value, handler.serialize, trailer);
      }
    });
  });
}

/**
 * Fully handle a server streaming call
 * @param {grpc.Call} call The call to handle
 * @param {Object} handler Request handler object for the method that was called
 * @param {Object} metadata Metadata from the client
 */
function handleServerStreaming(call, handler, metadata) {
  var stream = new ServerWritableStream(call, handler.serialize);
  waitForCancel(call, stream);
  var batch = {};
  batch[grpc.opType.SEND_INITIAL_METADATA] = metadata;
  batch[grpc.opType.RECV_MESSAGE] = true;
  call.startBatch(batch, function(err, result) {
    if (err) {
      stream.emit('error', err);
      return;
    }
    try {
      stream.request = handler.deserialize(result.read);
    } catch (e) {
      e.code = grpc.status.INVALID_ARGUMENT;
      stream.emit('error', e);
      return;
    }
    handler.func(stream);
  });
}

/**
 * Fully handle a client streaming call
 * @param {grpc.Call} call The call to handle
 * @param {Object} handler Request handler object for the method that was called
 * @param {Object} metadata Metadata from the client
 */
function handleClientStreaming(call, handler, metadata) {
  var stream = new ServerReadableStream(call, handler.deserialize);
  stream.on('error', function(error) {
    handleError(call, error);
  });
  waitForCancel(call, stream);
  var metadata_batch = {};
  metadata_batch[grpc.opType.SEND_INITIAL_METADATA] = metadata;
  call.startBatch(metadata_batch, function() {});
  handler.func(stream, function(err, value, trailer) {
    stream.terminate();
    if (err) {
      if (trailer) {
        err.metadata = trailer;
      }
      handleError(call, err);
    } else {
      sendUnaryResponse(call, value, handler.serialize, trailer);
    }
  });
}

/**
 * Fully handle a bidirectional streaming call
 * @param {grpc.Call} call The call to handle
 * @param {Object} handler Request handler object for the method that was called
 * @param {Object} metadata Metadata from the client
 */
function handleBidiStreaming(call, handler, metadata) {
  var stream = new ServerDuplexStream(call, handler.serialize,
                                      handler.deserialize);
  waitForCancel(call, stream);
  var metadata_batch = {};
  metadata_batch[grpc.opType.SEND_INITIAL_METADATA] = metadata;
  call.startBatch(metadata_batch, function() {});
  handler.func(stream);
}

var streamHandlers = {
  unary: handleUnary,
  server_stream: handleServerStreaming,
  client_stream: handleClientStreaming,
  bidi: handleBidiStreaming
};

/**
 * Constructs a server object that stores request handlers and delegates
 * incoming requests to those handlers
 * @constructor
 * @param {function(string, Object<string, Array<Buffer>>):
           Object<string, Array<Buffer|string>>=} getMetadata Callback that gets
 *     metatada for a given method
 * @param {Object=} options Options that should be passed to the internal server
 *     implementation
 */
function Server(getMetadata, options) {
  this.handlers = {};
  var handlers = this.handlers;
  var server = new grpc.Server(options);
  this._server = server;
  /**
   * Start the server and begin handling requests
   * @this Server
   */
  this.listen = function() {
    console.log('Server starting');
    _.each(handlers, function(handler, handler_name) {
      console.log('Serving', handler_name);
    });
    if (this.started) {
      throw 'Server is already running';
    }
    server.start();
    /**
     * Handles the SERVER_RPC_NEW event. If there is a handler associated with
     * the requested method, use that handler to respond to the request. Then
     * wait for the next request
     * @param {grpc.Event} event The event to handle with tag SERVER_RPC_NEW
     */
    function handleNewCall(err, event) {
      if (err) {
        return;
      }
      var details = event['new call'];
      var call = details.call;
      var method = details.method;
      var metadata = details.metadata;
      if (method === null) {
        return;
      }
      server.requestCall(handleNewCall);
      var handler;
      if (handlers.hasOwnProperty(method)) {
        handler = handlers[method];
      } else {
        var batch = {};
        batch[grpc.opType.SEND_INITIAL_METADATA] = {};
        batch[grpc.opType.SEND_STATUS_FROM_SERVER] = {
          code: grpc.status.UNIMPLEMENTED,
          details: 'This method is not available on this server.',
          metadata: {}
        };
        batch[grpc.opType.RECV_CLOSE_ON_SERVER] = true;
        call.startBatch(batch, function() {});
        return;
      }
      var response_metadata = {};
      if (getMetadata) {
        response_metadata = getMetadata(method, metadata);
      }
      streamHandlers[handler.type](call, handler, response_metadata);
    }
    server.requestCall(handleNewCall);
  };
  /** Shuts down the server.
   */
  this.shutdown = function() {
    server.shutdown();
  };
}

/**
 * Registers a handler to handle the named method. Fails if there already is
 * a handler for the given method. Returns true on success
 * @param {string} name The name of the method that the provided function should
 *     handle/respond to.
 * @param {function} handler Function that takes a stream of request values and
 *     returns a stream of response values
 * @param {function(*):Buffer} serialize Serialization function for responses
 * @param {function(Buffer):*} deserialize Deserialization function for requests
 * @param {string} type The streaming type of method that this handles
 * @return {boolean} True if the handler was set. False if a handler was already
 *     set for that name.
 */
Server.prototype.register = function(name, handler, serialize, deserialize,
                                     type) {
  if (this.handlers.hasOwnProperty(name)) {
    return false;
  }
  this.handlers[name] = {
    func: handler,
    serialize: serialize,
    deserialize: deserialize,
    type: type
  };
  return true;
};

/**
 * Binds the server to the given port, with SSL enabled if creds is given
 * @param {string} port The port that the server should bind on, in the format
 *     "address:port"
 * @param {boolean=} creds Server credential object to be used for SSL. Pass
 *     nothing for an insecure port
 */
Server.prototype.bind = function(port, creds) {
  if (creds) {
    return this._server.addSecureHttp2Port(port, creds);
  } else {
    return this._server.addHttp2Port(port);
  }
};

/**
 * Create a constructor for servers with services defined by service_attr_map.
 * That is an object that maps (namespaced) service names to objects that in
 * turn map method names to objects with the following keys:
 * path: The path on the server for accessing the method. For example, for
 *     protocol buffers, we use "/service_name/method_name"
 * requestStream: bool indicating whether the client sends a stream
 * resonseStream: bool indicating whether the server sends a stream
 * requestDeserialize: function to deserialize request objects
 * responseSerialize: function to serialize response objects
 * @param {Object} service_attr_map An object mapping service names to method
 *     attribute map objects
 * @return {function(Object, function, Object=)} New server constructor
 */
function makeServerConstructor(service_attr_map) {
  /**
   * Create a server with the given handlers for all of the methods.
   * @constructor
   * @param {Object} service_handlers Map from service names to map from method
   *     names to handlers
   * @param {function(string, Object<string, Array<Buffer>>):
             Object<string, Array<Buffer|string>>=} getMetadata Callback that
   *     gets metatada for a given method
   * @param {Object=} options Options to pass to the underlying server
   */
  function SurfaceServer(service_handlers, getMetadata, options) {
    var server = new Server(getMetadata, options);
    this.inner_server = server;
    _.each(service_attr_map, function(service_attrs, service_name) {
      if (service_handlers[service_name] === undefined) {
        throw new Error('Handlers for service ' +
            service_name + ' not provided.');
      }
      _.each(service_attrs, function(attrs, name) {
        var method_type;
        if (attrs.requestStream) {
          if (attrs.responseStream) {
            method_type = 'bidi';
          } else {
            method_type = 'client_stream';
          }
        } else {
          if (attrs.responseStream) {
            method_type = 'server_stream';
          } else {
            method_type = 'unary';
          }
        }
        if (service_handlers[service_name][name] === undefined) {
          throw new Error('Method handler for ' + attrs.path +
              ' not provided.');
        }
        var serialize = attrs.responseSerialize;
        var deserialize = attrs.requestDeserialize;
        server.register(attrs.path, _.bind(service_handlers[service_name][name],
                                           service_handlers[service_name]),
                        serialize, deserialize, method_type);
      });
    }, this);
  }

  /**
   * Binds the server to the given port, with SSL enabled if creds is supplied
   * @param {string} port The port that the server should bind on, in the format
   *     "address:port"
   * @param {boolean=} creds Credentials to use for SSL
   * @return {SurfaceServer} this
   */
  SurfaceServer.prototype.bind = function(port, creds) {
    return this.inner_server.bind(port, creds);
  };

  /**
   * Starts the server listening on any bound ports
   * @return {SurfaceServer} this
   */
  SurfaceServer.prototype.listen = function() {
    this.inner_server.listen();
    return this;
  };

  /**
   * Shuts the server down; tells it to stop listening for new requests and to
   * kill old requests.
   */
  SurfaceServer.prototype.shutdown = function() {
    this.inner_server.shutdown();
  };

  return SurfaceServer;
}

/**
 * Create a constructor for servers that serve the given services.
 * @param {Array<ProtoBuf.Reflect.Service>} services The services that the
 *     servers will serve
 * @return {function(Object, function, Object=)} New server constructor
 */
function makeProtobufServerConstructor(services) {
  var qual_names = [];
  var service_attr_map = {};
  _.each(services, function(service) {
    var service_name = common.fullyQualifiedName(service);
    _.each(service.children, function(method) {
      var name = common.fullyQualifiedName(method);
      if (_.indexOf(qual_names, name) !== -1) {
        throw new Error('Method ' + name + ' exposed by more than one service');
      }
      qual_names.push(name);
    });
    var method_attrs = common.getProtobufServiceAttrs(service);
    if (!service_attr_map.hasOwnProperty(service_name)) {
      service_attr_map[service_name] = {};
    }
    service_attr_map[service_name] = _.extend(service_attr_map[service_name],
                                              method_attrs);
  });
  return makeServerConstructor(service_attr_map);
}

/**
 * See documentation for makeServerConstructor
 */
exports.makeServerConstructor = makeServerConstructor;

/**
 * See documentation for makeProtobufServerConstructor
 */
exports.makeProtobufServerConstructor = makeProtobufServerConstructor;
