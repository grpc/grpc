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

var _ = require('underscore');

var capitalize = require('underscore.string/capitalize');
var decapitalize = require('underscore.string/decapitalize');

var grpc = require('bindings')('grpc.node');

var common = require('./common');

var stream = require('stream');

var Readable = stream.Readable;
var Writable = stream.Writable;
var Duplex = stream.Duplex;
var util = require('util');

var EventEmitter = require('events').EventEmitter;

var common = require('./common.js');

function handleError(call, error) {
  var error_batch = {};
  error_batch[grpc.opType.SEND_STATUS_FROM_SERVER] = {
    code: grpc.status.INTERNAL,
    details: 'Unknown Error',
    metadata: {}
  };
  call.startBatch(error_batch, function(){});
}

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

function sendUnaryResponse(call, value, serialize) {
  var end_batch = {};
  end_batch[grpc.opType.SEND_MESSAGE] = serialize(value);
  end_batch[grpc.opType.SEND_STATUS_FROM_SERVER] = {
    code: grpc.status.OK,
    details: 'OK',
    metadata: {}
  };
  call.startBatch(end_batch, function (){});
}

function setUpWritable(stream, serialize) {
  stream.finished = false;
  stream.status = {
    'code' : grpc.status.OK,
    'details' : 'OK'
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
   * code or details properties, the code will be set to grpc.status.INTERNAL
   * and the details will be set to 'Unknown Error'.
   * @param {Error} err The error object
   */
  function setStatus(err) {
    var code = grpc.status.INTERNAL;
    var details = 'Unknown Error';

    if (err.hasOwnProperty('code')) {
      code = err.code;
      if (err.hasOwnProperty('details')) {
        details = err.details;
      }
    }
    stream.status = {'code': code, 'details': details};
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
}

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
    if (self.push(self.deserialize(data)) && data != null) {
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

function ServerDuplexStream(call, serialize, deserialize) {
  Duplex.call(this, {objectMode: true});
  setUpWritable(this, serialize);
  setUpReadable(this, deserialize);
}

ServerDuplexStream.prototype._read = _read;
ServerDuplexStream.prototype._write = _write;

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
    emitter.request = handler.deserialize(result.read);
    if (emitter.cancelled) {
      return;
    }
    handler.func(emitter, function sendUnaryData(err, value) {
      if (err) {
        handleError(call, err);
      }
      sendUnaryResponse(call, value, handler.serialize);
    });
  });
}

function handleServerStreaming(call, handler, metadata) {
  console.log('Handling server streaming call');
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
    stream.request = result.read;
    handler.func(stream);
  });
}

function handleClientStreaming(call, handler, metadata) {
  var stream = new ServerReadableStream(call, handler.deserialize);
  waitForCancel(call, stream);
  var metadata_batch = {};
  metadata_batch[grpc.opType.SEND_INITIAL_METADATA] = metadata;
  call.startBatch(metadata_batch, function() {});
  handler.func(stream, function(err, value) {
    stream.terminate();
    if (err) {
      handleError(call, err);
    }
    sendUnaryResponse(call, value, handler.serialize);
  });
}

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
  var started = false;
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
      console.log('Handling new call');
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
      var handler = undefined;
      var deadline = details.deadline;
      if (handlers.hasOwnProperty(method)) {
        handler = handlers[method];
        console.log(handler);
      } else {
        console.log(handlers);
        var batch = {};
        batch[grpc.opType.SEND_INITIAL_METADATA] = {};
        batch[grpc.opType.SEND_STATUS_FROM_SERVER] = {
          code: grpc.status.UNIMPLEMENTED,
          details: "This method is not available on this server.",
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
 * Binds the server to the given port, with SSL enabled if secure is specified
 * @param {string} port The port that the server should bind on, in the format
 *     "address:port"
 * @param {boolean=} secure Whether the server should open a secure port
 */
Server.prototype.bind = function(port, secure) {
  if (secure) {
    return this._server.addSecureHttp2Port(port);
  } else {
    return this._server.addHttp2Port(port);
  }
};

/**
 * Creates a constructor for servers with a service defined by the methods
 * object. The methods object has string keys and values of this form:
 * {serialize: function, deserialize: function, client_stream: bool,
 *  server_stream: bool}
 * @param {Object} methods Method descriptor for each method the server should
 *     expose
 * @param {string} prefix The prefex to prepend to each method name
 * @return {function(Object, Object)} New server constructor
 */
function makeServerConstructor(services) {
  var qual_names = [];
  _.each(services, function(service) {
    _.each(service.children, function(method) {
      var name = common.fullyQualifiedName(method);
      if (_.indexOf(qual_names, name) !== -1) {
        throw new Error('Method ' + name + ' exposed by more than one service');
      }
      qual_names.push(name);
    });
  });
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
    _.each(services, function(service) {
      var service_name = common.fullyQualifiedName(service);
      if (service_handlers[service_name] === undefined) {
        throw new Error('Handlers for service ' +
            service_name + ' not provided.');
      }
      var prefix = '/' + common.fullyQualifiedName(service) + '/';
      _.each(service.children, function(method) {
        var method_type;
        if (method.requestStream) {
          if (method.responseStream) {
            method_type = 'bidi';
          } else {
            method_type = 'client_stream';
          }
        } else {
          if (method.responseStream) {
            method_type = 'server_stream';
          } else {
            method_type = 'unary';
          }
        }
        if (service_handlers[service_name][decapitalize(method.name)] ===
            undefined) {
          throw new Error('Method handler for ' +
              common.fullyQualifiedName(method) + ' not provided.');
        }
        var serialize = common.serializeCls(
            method.resolvedResponseType.build());
        var deserialize = common.deserializeCls(
            method.resolvedRequestType.build());
        server.register(
            prefix + capitalize(method.name),
            service_handlers[service_name][decapitalize(method.name)],
            serialize, deserialize, method_type);
      });
    }, this);
  }

  /**
   * Binds the server to the given port, with SSL enabled if secure is specified
   * @param {string} port The port that the server should bind on, in the format
   *     "address:port"
   * @param {boolean=} secure Whether the server should open a secure port
   * @return {SurfaceServer} this
   */
  SurfaceServer.prototype.bind = function(port, secure) {
    return this.inner_server.bind(port, secure);
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
 * See documentation for makeServerConstructor
 */
exports.makeServerConstructor = makeServerConstructor;
