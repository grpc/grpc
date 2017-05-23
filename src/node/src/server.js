/**
 * @license
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

var grpc = require('./grpc_extension');

var common = require('./common');

var Metadata = require('./metadata');

var constants = require('./constants');

var stream = require('stream');

var Readable = stream.Readable;
var Writable = stream.Writable;
var Duplex = stream.Duplex;
var util = require('util');

var EventEmitter = require('events').EventEmitter;

/**
 * Handle an error on a call by sending it as a status
 * @private
 * @param {grpc.internal~Call} call The call to send the error on
 * @param {(Object|Error)} error The error object
 */
function handleError(call, error) {
  var statusMetadata = new Metadata();
  var status = {
    code: constants.status.UNKNOWN,
    details: 'Unknown Error'
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
    statusMetadata = error.metadata;
  }
  status.metadata = statusMetadata._getCoreRepresentation();
  var error_batch = {};
  if (!call.metadataSent) {
    error_batch[grpc.opType.SEND_INITIAL_METADATA] =
        (new Metadata())._getCoreRepresentation();
  }
  error_batch[grpc.opType.SEND_STATUS_FROM_SERVER] = status;
  call.startBatch(error_batch, function(){});
}

/**
 * Send a response to a unary or client streaming call.
 * @private
 * @param {grpc.Call} call The call to respond on
 * @param {*} value The value to respond with
 * @param {grpc~serialize} serialize Serialization function for the
 *     response
 * @param {grpc.Metadata=} metadata Optional trailing metadata to send with
 *     status
 * @param {number=} [flags=0] Flags for modifying how the message is sent.
 */
function sendUnaryResponse(call, value, serialize, metadata, flags) {
  var end_batch = {};
  var statusMetadata = new Metadata();
  var status = {
    code: constants.status.OK,
    details: 'OK'
  };
  if (metadata) {
    statusMetadata = metadata;
  }
  var message;
  try {
    message = serialize(value);
  } catch (e) {
    e.code = constants.status.INTERNAL;
    handleError(call, e);
    return;
  }
  status.metadata = statusMetadata._getCoreRepresentation();
  if (!call.metadataSent) {
    end_batch[grpc.opType.SEND_INITIAL_METADATA] =
        (new Metadata())._getCoreRepresentation();
    call.metadataSent = true;
  }
  message.grpcWriteFlags = flags;
  end_batch[grpc.opType.SEND_MESSAGE] = message;
  end_batch[grpc.opType.SEND_STATUS_FROM_SERVER] = status;
  call.startBatch(end_batch, function (){});
}

/**
 * Initialize a writable stream. This is used for both the writable and duplex
 * stream constructors.
 * @private
 * @param {Writable} stream The stream to set up
 * @param {function(*):Buffer=} Serialization function for responses
 */
function setUpWritable(stream, serialize) {
  stream.finished = false;
  stream.status = {
    code : constants.status.OK,
    details : 'OK',
    metadata : new Metadata()
  };
  stream.serialize = common.wrapIgnoreNull(serialize);
  function sendStatus() {
    var batch = {};
    if (!stream.call.metadataSent) {
      stream.call.metadataSent = true;
      batch[grpc.opType.SEND_INITIAL_METADATA] =
          (new Metadata())._getCoreRepresentation();
    }

    if (stream.status.metadata) {
      stream.status.metadata = stream.status.metadata._getCoreRepresentation();
    }
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
    var code = constants.status.UNKNOWN;
    var details = 'Unknown Error';
    var metadata = new Metadata();
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
   * @param {Metadata=} metadata Metadata to send with the status
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
 * @private
 * @param {Readable} stream The stream to initialize
 * @param {grpc~deserialize} deserialize Deserialization function for
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

/**
 * Emitted when the call has been cancelled. After this has been emitted, the
 * call's `cancelled` property will be set to `true`.
 * @event grpc~ServerUnaryCall~cancelled
 */

util.inherits(ServerUnaryCall, EventEmitter);

/**
 * An EventEmitter. Used for unary calls.
 * @constructor grpc~ServerUnaryCall
 * @extends external:EventEmitter
 * @param {grpc.internal~Call} call The call object associated with the request
 * @param {grpc.Metadata} metadata The request metadata from the client
 */
function ServerUnaryCall(call, metadata) {
  EventEmitter.call(this);
  this.call = call;
  /**
   * Indicates if the call has been cancelled
   * @member {boolean} grpc~ServerUnaryCall#cancelled
   */
  this.cancelled = false;
  /**
   * The request metadata from the client
   * @member {grpc.Metadata} grpc~ServerUnaryCall#metadata
   */
  this.metadata = metadata;
  /**
   * The request message from the client
   * @member {*} grpc~ServerUnaryCall#request
   */
  this.request = undefined;
}

/**
 * Emitted when the call has been cancelled. After this has been emitted, the
 * call's `cancelled` property will be set to `true`.
 * @event grpc~ServerWritableStream~cancelled
 */

util.inherits(ServerWritableStream, Writable);

/**
 * A stream that the server can write to. Used for calls that are streaming from
 * the server side.
 * @constructor grpc~ServerWritableStream
 * @extends external:Writable
 * @borrows grpc~ServerUnaryCall#sendMetadata as
 *     grpc~ServerWritableStream#sendMetadata
 * @borrows grpc~ServerUnaryCall#getPeer as grpc~ServerWritableStream#getPeer
 * @param {grpc.internal~Call} call The call object to send data with
 * @param {grpc.Metadata} metadata The request metadata from the client
 * @param {grpc~serialize} serialize Serialization function for writes
 */
function ServerWritableStream(call, metadata, serialize) {
  Writable.call(this, {objectMode: true});
  this.call = call;

  this.finished = false;
  setUpWritable(this, serialize);
  /**
   * Indicates if the call has been cancelled
   * @member {boolean} grpc~ServerWritableStream#cancelled
   */
  this.cancelled = false;
  /**
   * The request metadata from the client
   * @member {grpc.Metadata} grpc~ServerWritableStream#metadata
   */
  this.metadata = metadata;
  /**
   * The request message from the client
   * @member {*} grpc~ServerWritableStream#request
   */
  this.request = undefined;
}

/**
 * Start writing a chunk of data. This is an implementation of a method required
 * for implementing stream.Writable.
 * @private
 * @param {Buffer} chunk The chunk of data to write
 * @param {string} encoding Used to pass write flags
 * @param {function(Error=)} callback Callback to indicate that the write is
 *     complete
 */
function _write(chunk, encoding, callback) {
  /* jshint validthis: true */
  var batch = {};
  var self = this;
  var message;
  try {
    message = this.serialize(chunk);
  } catch (e) {
    e.code = constants.status.INTERNAL;
    callback(e);
    return;
  }
  if (!this.call.metadataSent) {
    batch[grpc.opType.SEND_INITIAL_METADATA] =
        (new Metadata())._getCoreRepresentation();
    this.call.metadataSent = true;
  }
  if (_.isFinite(encoding)) {
    /* Attach the encoding if it is a finite number. This is the closest we
     * can get to checking that it is valid flags */
    message.grpcWriteFlags = encoding;
  }
  batch[grpc.opType.SEND_MESSAGE] = message;
  this.call.startBatch(batch, function(err, value) {
    if (err) {
      self.emit('error', err);
      return;
    }
    callback();
  });
}

ServerWritableStream.prototype._write = _write;

/**
 * Emitted when the call has been cancelled. After this has been emitted, the
 * call's `cancelled` property will be set to `true`.
 * @event grpc~ServerReadableStream~cancelled
 */

util.inherits(ServerReadableStream, Readable);

/**
 * A stream that the server can read from. Used for calls that are streaming
 * from the client side.
 * @constructor grpc~ServerReadableStream
 * @extends external:Readable
 * @borrows grpc~ServerUnaryCall#sendMetadata as
 *     grpc~ServerReadableStream#sendMetadata
 * @borrows grpc~ServerUnaryCall#getPeer as grpc~ServerReadableStream#getPeer
 * @param {grpc.internal~Call} call The call object to read data with
 * @param {grpc.Metadata} metadata The request metadata from the client
 * @param {grpc~deserialize} deserialize Deserialization function for reads
 */
function ServerReadableStream(call, metadata, deserialize) {
  Readable.call(this, {objectMode: true});
  this.call = call;
  setUpReadable(this, deserialize);
  /**
   * Indicates if the call has been cancelled
   * @member {boolean} grpc~ServerReadableStream#cancelled
   */
  this.cancelled = false;
  /**
   * The request metadata from the client
   * @member {grpc.Metadata} grpc~ServerReadableStream#metadata
   */
  this.metadata = metadata;
}

/**
 * Start reading from the gRPC data source. This is an implementation of a
 * method required for implementing stream.Readable
 * @access private
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
      e.code = constants.status.INTERNAL;
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

/**
 * Emitted when the call has been cancelled. After this has been emitted, the
 * call's `cancelled` property will be set to `true`.
 * @event grpc~ServerDuplexStream~cancelled
 */

util.inherits(ServerDuplexStream, Duplex);

/**
 * A stream that the server can read from or write to. Used for calls with
 * duplex streaming.
 * @constructor grpc~ServerDuplexStream
 * @extends external:Duplex
 * @borrows grpc~ServerUnaryCall#sendMetadata as
 *     grpc~ServerDuplexStream#sendMetadata
 * @borrows grpc~ServerUnaryCall#getPeer as grpc~ServerDuplexStream#getPeer
 * @param {grpc.internal~Call} call Call object to proxy
 * @param {grpc.Metadata} metadata The request metadata from the client
 * @param {grpc~serialize} serialize Serialization function for requests
 * @param {grpc~deserialize} deserialize Deserialization function for
 *     responses
 */
function ServerDuplexStream(call, metadata, serialize, deserialize) {
  Duplex.call(this, {objectMode: true});
  this.call = call;
  setUpWritable(this, serialize);
  setUpReadable(this, deserialize);
  /**
   * Indicates if the call has been cancelled
   * @member {boolean} grpc~ServerReadableStream#cancelled
   */
  this.cancelled = false;
  /**
   * The request metadata from the client
   * @member {grpc.Metadata} grpc~ServerReadableStream#metadata
   */
  this.metadata = metadata;
}

ServerDuplexStream.prototype._read = _read;
ServerDuplexStream.prototype._write = _write;

/**
 * Send the initial metadata for a writable stream.
 * @alias grpc~ServerUnaryCall#sendMetadata
 * @param {Metadata} responseMetadata Metadata to send
 */
function sendMetadata(responseMetadata) {
  /* jshint validthis: true */
  var self = this;
  if (!this.call.metadataSent) {
    this.call.metadataSent = true;
    var batch = {};
    batch[grpc.opType.SEND_INITIAL_METADATA] =
        responseMetadata._getCoreRepresentation();
    this.call.startBatch(batch, function(err) {
      if (err) {
        self.emit('error', err);
        return;
      }
    });
  }
}

ServerUnaryCall.prototype.sendMetadata = sendMetadata;
ServerWritableStream.prototype.sendMetadata = sendMetadata;
ServerReadableStream.prototype.sendMetadata = sendMetadata;
ServerDuplexStream.prototype.sendMetadata = sendMetadata;

/**
 * Get the endpoint this call/stream is connected to.
 * @alias grpc~ServerUnaryCall#getPeer
 * @return {string} The URI of the endpoint
 */
function getPeer() {
  /* jshint validthis: true */
  return this.call.getPeer();
}

ServerUnaryCall.prototype.getPeer = getPeer;
ServerReadableStream.prototype.getPeer = getPeer;
ServerWritableStream.prototype.getPeer = getPeer;
ServerDuplexStream.prototype.getPeer = getPeer;

/**
 * Wait for the client to close, then emit a cancelled event if the client
 * cancelled.
 * @private
 */
function waitForCancel() {
  /* jshint validthis: true */
  var self = this;
  var cancel_batch = {};
  cancel_batch[grpc.opType.RECV_CLOSE_ON_SERVER] = true;
  self.call.startBatch(cancel_batch, function(err, result) {
    if (err) {
      self.emit('error', err);
    }
    if (result.cancelled) {
      self.cancelled = true;
      self.emit('cancelled');
    }
  });
}

ServerUnaryCall.prototype.waitForCancel = waitForCancel;
ServerReadableStream.prototype.waitForCancel = waitForCancel;
ServerWritableStream.prototype.waitForCancel = waitForCancel;
ServerDuplexStream.prototype.waitForCancel = waitForCancel;

/**
 * Callback function passed to server handlers that handle methods with unary
 * responses.
 * @callback grpc.Server~sendUnaryData
 * @param {grpc~ServiceError} error An error, if the call failed
 * @param {*} value The response value. Must be a valid argument to the
 *     `responseSerialize` method of the method that is being handled
 * @param {grpc.Metadata=} trailer Trailing metadata to send, if applicable
 * @param {grpc.writeFlags=} flags Flags to modify writing the response
 */

/**
 * User-provided method to handle unary requests on a server
 * @callback grpc.Server~handleUnaryCall
 * @param {grpc~ServerUnaryCall} call The call object
 * @param {grpc.Server~sendUnaryData} callback The callback to call to respond
 *     to the request
 */

/**
 * Fully handle a unary call
 * @private
 * @param {grpc.internal~Call} call The call to handle
 * @param {Object} handler Request handler object for the method that was called
 * @param {grpc~Server.handleUnaryCall} handler.func The handler function
 * @param {grpc~deserialize} handler.deserialize The deserialization function
 *     for request data
 * @param {grpc~serialize} handler.serialize The serialization function for
 *     response data
 * @param {grpc.Metadata} metadata Metadata from the client
 */
function handleUnary(call, handler, metadata) {
  var emitter = new ServerUnaryCall(call, metadata);
  emitter.on('error', function(error) {
    handleError(call, error);
  });
  emitter.waitForCancel();
  var batch = {};
  batch[grpc.opType.RECV_MESSAGE] = true;
  call.startBatch(batch, function(err, result) {
    if (err) {
      handleError(call, err);
      return;
    }
    try {
      emitter.request = handler.deserialize(result.read);
    } catch (e) {
      e.code = constants.status.INTERNAL;
      handleError(call, e);
      return;
    }
    if (emitter.cancelled) {
      return;
    }
    handler.func(emitter, function sendUnaryData(err, value, trailer, flags) {
      if (err) {
        if (trailer) {
          err.metadata = trailer;
        }
        handleError(call, err);
      } else {
        sendUnaryResponse(call, value, handler.serialize, trailer, flags);
      }
    });
  });
}

/**
 * User provided method to handle server streaming methods on the server.
 * @callback grpc.Server~handleServerStreamingCall
 * @param {grpc~ServerWritableStream} call The call object
 */

/**
 * Fully handle a server streaming call
 * @private
 * @param {grpc.internal~Call} call The call to handle
 * @param {Object} handler Request handler object for the method that was called
 * @param {grpc~Server.handleServerStreamingCall} handler.func The handler
 *     function
 * @param {grpc~deserialize} handler.deserialize The deserialization function
 *     for request data
 * @param {grpc~serialize} handler.serialize The serialization function for
 *     response data
 * @param {grpc.Metadata} metadata Metadata from the client
 */
function handleServerStreaming(call, handler, metadata) {
  var stream = new ServerWritableStream(call, metadata, handler.serialize);
  stream.waitForCancel();
  var batch = {};
  batch[grpc.opType.RECV_MESSAGE] = true;
  call.startBatch(batch, function(err, result) {
    if (err) {
      stream.emit('error', err);
      return;
    }
    try {
      stream.request = handler.deserialize(result.read);
    } catch (e) {
      e.code = constants.status.INTERNAL;
      stream.emit('error', e);
      return;
    }
    handler.func(stream);
  });
}

/**
 * User provided method to handle client streaming methods on the server.
 * @callback grpc.Server~handleClientStreamingCall
 * @param {grpc~ServerReadableStream} call The call object
 * @param {grpc.Server~sendUnaryData} callback The callback to call to respond
 *     to the request
 */

/**
 * Fully handle a client streaming call
 * @access private
 * @param {grpc.internal~Call} call The call to handle
 * @param {Object} handler Request handler object for the method that was called
 * @param {grpc~Server.handleClientStreamingCall} handler.func The handler
 *     function
 * @param {grpc~deserialize} handler.deserialize The deserialization function
 *     for request data
 * @param {grpc~serialize} handler.serialize The serialization function for
 *     response data
 * @param {grpc.Metadata} metadata Metadata from the client
 */
function handleClientStreaming(call, handler, metadata) {
  var stream = new ServerReadableStream(call, metadata, handler.deserialize);
  stream.on('error', function(error) {
    handleError(call, error);
  });
  stream.waitForCancel();
  handler.func(stream, function(err, value, trailer, flags) {
    stream.terminate();
    if (err) {
      if (trailer) {
        err.metadata = trailer;
      }
      handleError(call, err);
    } else {
      sendUnaryResponse(call, value, handler.serialize, trailer, flags);
    }
  });
}

/**
 * User provided method to handle bidirectional streaming calls on the server.
 * @callback grpc.Server~handleBidiStreamingCall
 * @param {grpc~ServerDuplexStream} call The call object
 */

/**
 * Fully handle a bidirectional streaming call
 * @private
 * @param {grpc.internal~Call} call The call to handle
 * @param {Object} handler Request handler object for the method that was called
 * @param {grpc~Server.handleBidiStreamingCall} handler.func The handler
 *     function
 * @param {grpc~deserialize} handler.deserialize The deserialization function
 *     for request data
 * @param {grpc~serialize} handler.serialize The serialization function for
 *     response data
 * @param {Metadata} metadata Metadata from the client
 */
function handleBidiStreaming(call, handler, metadata) {
  var stream = new ServerDuplexStream(call, metadata, handler.serialize,
                                      handler.deserialize);
  stream.waitForCancel();
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
 * @memberof grpc
 * @constructor
 * @param {Object=} options Options that should be passed to the internal server
 *     implementation
 * @example
 * var server = new grpc.Server();
 * server.addProtoService(protobuf_service_descriptor, service_implementation);
 * server.bind('address:port', server_credential);
 * server.start();
 */
function Server(options) {
  this.handlers = {};
  var server = new grpc.Server(options);
  this._server = server;
  this.started = false;
}

/**
 * Start the server and begin handling requests
 */
Server.prototype.start = function() {
  if (this.started) {
    throw new Error('Server is already running');
  }
  var self = this;
  this.started = true;
  this._server.start();
  /**
   * Handles the SERVER_RPC_NEW event. If there is a handler associated with
   * the requested method, use that handler to respond to the request. Then
   * wait for the next request
   * @param {grpc.internal~Event} event The event to handle with tag
   *     SERVER_RPC_NEW
   */
  function handleNewCall(err, event) {
    if (err) {
      return;
    }
    var details = event.new_call;
    var call = details.call;
    var method = details.method;
    var metadata = Metadata._fromCoreRepresentation(details.metadata);
    if (method === null) {
      return;
    }
    self._server.requestCall(handleNewCall);
    var handler;
    if (self.handlers.hasOwnProperty(method)) {
      handler = self.handlers[method];
    } else {
      var batch = {};
      batch[grpc.opType.SEND_INITIAL_METADATA] =
          (new Metadata())._getCoreRepresentation();
      batch[grpc.opType.SEND_STATUS_FROM_SERVER] = {
        code: constants.status.UNIMPLEMENTED,
        details: '',
        metadata: {}
      };
      batch[grpc.opType.RECV_CLOSE_ON_SERVER] = true;
      call.startBatch(batch, function() {});
      return;
    }
    streamHandlers[handler.type](call, handler, metadata);
  }
  this._server.requestCall(handleNewCall);
};

/**
 * Unified type for application handlers for all types of calls
 * @typedef {(grpc.Server~handleUnaryCall
 *            |grpc.Server~handleClientStreamingCall
 *            |grpc.Server~handleServerStreamingCall
 *            |grpc.Server~handleBidiStreamingCall)} grpc.Server~handleCall
 */

/**
 * Registers a handler to handle the named method. Fails if there already is
 * a handler for the given method. Returns true on success
 * @param {string} name The name of the method that the provided function should
 *     handle/respond to.
 * @param {grpc.Server~handleCall} handler Function that takes a stream of
 *     request values and returns a stream of response values
 * @param {grpc~serialize} serialize Serialization function for responses
 * @param {grpc~deserialize} deserialize Deserialization function for requests
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
 * Gracefully shuts down the server. The server will stop receiving new calls,
 * and any pending calls will complete. The callback will be called when all
 * pending calls have completed and the server is fully shut down. This method
 * is idempotent with itself and forceShutdown.
 * @param {function()} callback The shutdown complete callback
 */
Server.prototype.tryShutdown = function(callback) {
  this._server.tryShutdown(callback);
};

/**
 * Forcibly shuts down the server. The server will stop receiving new calls
 * and cancel all pending calls. When it returns, the server has shut down.
 * This method is idempotent with itself and tryShutdown, and it will trigger
 * any outstanding tryShutdown callbacks.
 */
Server.prototype.forceShutdown = function() {
  this._server.forceShutdown();
};

var unimplementedStatusResponse = {
  code: constants.status.UNIMPLEMENTED,
  details: 'The server does not implement this method'
};

var defaultHandler = {
  unary: function(call, callback) {
    callback(unimplementedStatusResponse);
  },
  client_stream: function(call, callback) {
    callback(unimplementedStatusResponse);
  },
  server_stream: function(call) {
    call.emit('error', unimplementedStatusResponse);
  },
  bidi: function(call) {
    call.emit('error', unimplementedStatusResponse);
  }
};

/**
 * Add a service to the server, with a corresponding implementation.
 * @param {grpc~ServiceDefinition} service The service descriptor
 * @param {Object<String, grpc.Server~handleCall>} implementation Map of method
 *     names to method implementation for the provided service.
 */
Server.prototype.addService = function(service, implementation) {
  if (!_.isObject(service) || !_.isObject(implementation)) {
    throw new Error('addService requires two objects as arguments');
  }
  if (_.keys(service).length === 0) {
    throw new Error('Cannot add an empty service to a server');
  }
  if (this.started) {
    throw new Error('Can\'t add a service to a started server.');
  }
  var self = this;
  _.forOwn(service, function(attrs, name) {
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
    var impl;
    if (implementation[name] === undefined) {
      /* Handle the case where the method is passed with the name exactly as
         written in the proto file, instead of using JavaScript function
         naming style */
      if (implementation[attrs.originalName] === undefined) {
        common.log(constants.logVerbosity.ERROR, 'Method handler ' + name +
            ' for ' + attrs.path + ' expected but not provided');
        impl = defaultHandler[method_type];
      } else {
        impl = _.bind(implementation[attrs.originalName], implementation);
      }
    } else {
      impl = _.bind(implementation[name], implementation);
    }
    var serialize = attrs.responseSerialize;
    var deserialize = attrs.requestDeserialize;
    var register_success = self.register(attrs.path, impl, serialize,
                                         deserialize, method_type);
    if (!register_success) {
      throw new Error('Method handler for ' + attrs.path +
          ' already provided.');
    }
  });
};

var logAddProtoServiceDeprecationOnce = _.once(function() {
    common.log(constants.logVerbosity.INFO,
               'Server#addProtoService is deprecated. Use addService instead');
});

/**
 * Add a proto service to the server, with a corresponding implementation
 * @deprecated Use {@link grpc.Server#addService} instead
 * @param {Protobuf.Reflect.Service} service The proto service descriptor
 * @param {Object<String, grpc.Server~handleCall>} implementation Map of method
 *     names to method implementation for the provided service.
 */
Server.prototype.addProtoService = function(service, implementation) {
  var options;
  var protobuf_js_5_common = require('./protobuf_js_5_common');
  var protobuf_js_6_common = require('./protobuf_js_6_common');
  logAddProtoServiceDeprecationOnce();
  if (protobuf_js_5_common.isProbablyProtobufJs5(service)) {
    options = _.defaults(service.grpc_options, common.defaultGrpcOptions);
    this.addService(
        protobuf_js_5_common.getProtobufServiceAttrs(service, options),
        implementation);
  } else if (protobuf_js_6_common.isProbablyProtobufJs6(service)) {
    options = _.defaults(service.grpc_options, common.defaultGrpcOptions);
    this.addService(
        protobuf_js_6_common.getProtobufServiceAttrs(service, options),
        implementation);
  } else {
    // We assume that this is a service attributes object
    this.addService(service, implementation);
  }
};

/**
 * Binds the server to the given port, with SSL disabled if creds is an
 * insecure credentials object
 * @param {string} port The port that the server should bind on, in the format
 *     "address:port"
 * @param {grpc.ServerCredentials} creds Server credential object to be used for
 *     SSL. Pass an insecure credentials object for an insecure port.
 */
Server.prototype.bind = function(port, creds) {
  if (this.started) {
    throw new Error('Can\'t bind an already running server to an address');
  }
  return this._server.addHttp2Port(port, creds);
};

exports.Server = Server;
