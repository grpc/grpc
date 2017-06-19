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

/**
 * Client module
 *
 * This module contains the factory method for creating Client classes, and the
 * method calling code for all types of methods.
 *
 * @example <caption>Create a client and call a method on it</caption>
 *
 * var proto_obj = grpc.load(proto_file_path);
 * var Client = proto_obj.package.subpackage.ServiceName;
 * var client = new Client(server_address, client_credentials);
 * var call = client.unaryMethod(arguments, callback);
 */

'use strict';

var _ = require('lodash');
var arguejs = require('arguejs');

var grpc = require('./grpc_extension');

var common = require('./common');

var Metadata = require('./metadata');

var constants = require('./constants');

var EventEmitter = require('events').EventEmitter;

var stream = require('stream');

var Readable = stream.Readable;
var Writable = stream.Writable;
var Duplex = stream.Duplex;
var util = require('util');
var version = require('../../../package.json').version;

/**
 * Initial response metadata sent by the server when it starts processing the
 * call
 * @event grpc~ClientUnaryCall#metadata
 * @type {grpc.Metadata}
 */

/**
 * Status of the call when it has completed.
 * @event grpc~ClientUnaryCall#status
 * @type grpc~StatusObject
 */

util.inherits(ClientUnaryCall, EventEmitter);

/**
 * An EventEmitter. Used for unary calls.
 * @constructor grpc~ClientUnaryCall
 * @extends external:EventEmitter
 * @param {grpc.internal~Call} call The call object associated with the request
 */
function ClientUnaryCall(call) {
  EventEmitter.call(this);
  this.call = call;
}

util.inherits(ClientWritableStream, Writable);

/**
 * A stream that the client can write to. Used for calls that are streaming from
 * the client side.
 * @constructor grpc~ClientWritableStream
 * @extends external:Writable
 * @borrows grpc~ClientUnaryCall#cancel as grpc~ClientWritableStream#cancel
 * @borrows grpc~ClientUnaryCall#getPeer as grpc~ClientWritableStream#getPeer
 * @borrows grpc~ClientUnaryCall#event:metadata as
 *     grpc~ClientWritableStream#metadata
 * @borrows grpc~ClientUnaryCall#event:status as
 *     grpc~ClientWritableStream#status
 * @param {grpc.internal~Call} call The call object to send data with
 * @param {grpc~serialize=} [serialize=identity] Serialization
 *     function for writes.
 */
function ClientWritableStream(call, serialize) {
  Writable.call(this, {objectMode: true});
  this.call = call;
  this.serialize = common.wrapIgnoreNull(serialize);
  this.on('finish', function() {
    var batch = {};
    batch[grpc.opType.SEND_CLOSE_FROM_CLIENT] = true;
    call.startBatch(batch, function() {});
  });
}

/**
 * Write a message to the request stream. If serializing the argument fails,
 * the call will be cancelled and the stream will end with an error.
 * @name grpc~ClientWritableStream#write
 * @kind function
 * @override
 * @param {*} message The message to write. Must be a valid argument to the
 *     serialize function of the corresponding method
 * @param {grpc.writeFlags} flags Flags to modify how the message is written
 * @param {Function} callback Callback for when this chunk of data is flushed
 * @return {boolean} As defined for [Writable]{@link external:Writable}
 */

/**
 * Attempt to write the given chunk. Calls the callback when done. This is an
 * implementation of a method needed for implementing stream.Writable.
 * @private
 * @param {*} chunk The chunk to write
 * @param {grpc.writeFlags} encoding Used to pass write flags
 * @param {function(Error=)} callback Called when the write is complete
 */
function _write(chunk, encoding, callback) {
  /* jshint validthis: true */
  var batch = {};
  var message;
  var self = this;
  if (this.writeFailed) {
    /* Once a write fails, just call the callback immediately to let the caller
       flush any pending writes. */
    setImmediate(callback);
    return;
  }
  try {
    message = this.serialize(chunk);
  } catch (e) {
    /* Sending this error to the server and emitting it immediately on the
       client may put the call in a slightly weird state on the client side,
       but passing an object that causes a serialization failure is a misuse
       of the API anyway, so that's OK. The primary purpose here is to give the
       programmer a useful error and to stop the stream properly */
    this.call.cancelWithStatus(constants.status.INTERNAL,
                               'Serialization failure');
    callback(e);
    return;
  }
  if (_.isFinite(encoding)) {
    /* Attach the encoding if it is a finite number. This is the closest we
     * can get to checking that it is valid flags */
    message.grpcWriteFlags = encoding;
  }
  batch[grpc.opType.SEND_MESSAGE] = message;
  this.call.startBatch(batch, function(err, event) {
    if (err) {
      /* Assume that the call is complete and that writing failed because a
         status was received. In that case, set a flag to discard all future
         writes */
      self.writeFailed = true;
    }
    callback();
  });
}

ClientWritableStream.prototype._write = _write;

util.inherits(ClientReadableStream, Readable);

/**
 * A stream that the client can read from. Used for calls that are streaming
 * from the server side.
 * @constructor grpc~ClientReadableStream
 * @extends external:Readable
 * @borrows grpc~ClientUnaryCall#cancel as grpc~ClientReadableStream#cancel
 * @borrows grpc~ClientUnaryCall#getPeer as grpc~ClientReadableStream#getPeer
 * @borrows grpc~ClientUnaryCall#event:metadata as
 *     grpc~ClientReadableStream#metadata
 * @borrows grpc~ClientUnaryCall#event:status as
 *     grpc~ClientReadableStream#status
 * @param {grpc.internal~Call} call The call object to read data with
 * @param {grpc~deserialize=} [deserialize=identity]
 *     Deserialization function for reads
 */
function ClientReadableStream(call, deserialize) {
  Readable.call(this, {objectMode: true});
  this.call = call;
  this.finished = false;
  this.reading = false;
  this.deserialize = common.wrapIgnoreNull(deserialize);
  /* Status generated from reading messages from the server. Overrides the
   * status from the server if not OK */
  this.read_status = null;
  /* Status received from the server. */
  this.received_status = null;
}

/**
 * Called when all messages from the server have been processed. The status
 * parameter indicates that the call should end with that status. status
 * defaults to OK if not provided.
 * @param {Object!} status The status that the call should end with
 * @private
 */
function _readsDone(status) {
  /* jshint validthis: true */
  if (!status) {
    status = {code: constants.status.OK, details: 'OK'};
  }
  if (status.code !== constants.status.OK) {
    this.call.cancelWithStatus(status.code, status.details);
  }
  this.finished = true;
  this.read_status = status;
  this._emitStatusIfDone();
}

ClientReadableStream.prototype._readsDone = _readsDone;

/**
 * Called to indicate that we have received a status from the server.
 * @private
 */
function _receiveStatus(status) {
  /* jshint validthis: true */
  this.received_status = status;
  this._emitStatusIfDone();
}

ClientReadableStream.prototype._receiveStatus = _receiveStatus;

/**
 * If we have both processed all incoming messages and received the status from
 * the server, emit the status. Otherwise, do nothing.
 * @private
 */
function _emitStatusIfDone() {
  /* jshint validthis: true */
  var status;
  if (this.read_status && this.received_status) {
    if (this.read_status.code !== constants.status.OK) {
      status = this.read_status;
    } else {
      status = this.received_status;
    }
    if (status.code === constants.status.OK) {
      this.push(null);
    } else {
      var error = new Error(status.details);
      error.code = status.code;
      error.metadata = status.metadata;
      this.emit('error', error);
    }
    this.emit('status', status);
  }
}

ClientReadableStream.prototype._emitStatusIfDone = _emitStatusIfDone;

/**
 * Read the next object from the stream.
 * @private
 * @param {*} size Ignored because we use objectMode=true
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
      // Something has gone wrong. Stop reading and wait for status
      self.finished = true;
      self._readsDone();
      return;
    }
    var data = event.read;
    var deserialized;
    try {
      deserialized = self.deserialize(data);
    } catch (e) {
      self._readsDone({code: constants.status.INTERNAL,
                       details: 'Failed to parse server response'});
      return;
    }
    if (data === null) {
      self._readsDone();
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
      var read_batch = {};
      read_batch[grpc.opType.RECV_MESSAGE] = true;
      self.call.startBatch(read_batch, readCallback);
    }
  }
}

ClientReadableStream.prototype._read = _read;

util.inherits(ClientDuplexStream, Duplex);

/**
 * A stream that the client can read from or write to. Used for calls with
 * duplex streaming.
 * @constructor grpc~ClientDuplexStream
 * @extends external:Duplex
 * @borrows grpc~ClientUnaryCall#cancel as grpc~ClientDuplexStream#cancel
 * @borrows grpc~ClientUnaryCall#getPeer as grpc~ClientDuplexStream#getPeer
 * @borrows grpc~ClientWritableStream#write as grpc~ClientDuplexStream#write
 * @borrows grpc~ClientUnaryCall#event:metadata as
 *     grpc~ClientDuplexStream#metadata
 * @borrows grpc~ClientUnaryCall#event:status as
 *     grpc~ClientDuplexStream#status
 * @param {grpc.internal~Call} call Call object to proxy
 * @param {grpc~serialize=} [serialize=identity] Serialization
 *     function for requests
 * @param {grpc~deserialize=} [deserialize=identity]
 *     Deserialization function for responses
 */
function ClientDuplexStream(call, serialize, deserialize) {
  Duplex.call(this, {objectMode: true});
  this.serialize = common.wrapIgnoreNull(serialize);
  this.deserialize = common.wrapIgnoreNull(deserialize);
  this.call = call;
  /* Status generated from reading messages from the server. Overrides the
   * status from the server if not OK */
  this.read_status = null;
  /* Status received from the server. */
  this.received_status = null;
  this.on('finish', function() {
    var batch = {};
    batch[grpc.opType.SEND_CLOSE_FROM_CLIENT] = true;
    call.startBatch(batch, function() {});
  });
}

ClientDuplexStream.prototype._readsDone = _readsDone;
ClientDuplexStream.prototype._receiveStatus = _receiveStatus;
ClientDuplexStream.prototype._emitStatusIfDone = _emitStatusIfDone;
ClientDuplexStream.prototype._read = _read;
ClientDuplexStream.prototype._write = _write;

/**
 * Cancel the ongoing call. Results in the call ending with a CANCELLED status,
 * unless it has already ended with some other status.
 * @alias grpc~ClientUnaryCall#cancel
 */
function cancel() {
  /* jshint validthis: true */
  this.call.cancel();
}

ClientUnaryCall.prototype.cancel = cancel;
ClientReadableStream.prototype.cancel = cancel;
ClientWritableStream.prototype.cancel = cancel;
ClientDuplexStream.prototype.cancel = cancel;

/**
 * Get the endpoint this call/stream is connected to.
 * @return {string} The URI of the endpoint
 * @alias grpc~ClientUnaryCall#getPeer
 */
function getPeer() {
  /* jshint validthis: true */
  return this.call.getPeer();
}

ClientUnaryCall.prototype.getPeer = getPeer;
ClientReadableStream.prototype.getPeer = getPeer;
ClientWritableStream.prototype.getPeer = getPeer;
ClientDuplexStream.prototype.getPeer = getPeer;

/**
 * Any client call type
 * @typedef {(ClientUnaryCall|ClientReadableStream|
 *            ClientWritableStream|ClientDuplexStream)}
 *     grpc.Client~Call
 */

/**
 * Options that can be set on a call.
 * @typedef {Object} grpc.Client~CallOptions
 * @property {grpc~Deadline} deadline The deadline for the entire call to
 *     complete.
 * @property {string} host Server hostname to set on the call. Only meaningful
 *     if different from the server address used to construct the client.
 * @property {grpc.Client~Call} parent Parent call. Used in servers when
 *     making a call as part of the process of handling a call. Used to
 *     propagate some information automatically, as specified by
 *     propagate_flags.
 * @property {number} propagate_flags Indicates which properties of a parent
 *     call should propagate to this call. Bitwise combination of flags in
 *     {@link grpc.propagate}.
 * @property {grpc.credentials~CallCredentials} credentials The credentials that
 *     should be used to make this particular call.
 */

/**
 * Get a call object built with the provided options.
 * @access private
 * @param {grpc.Client~CallOptions=} options Options object.
 */
function getCall(channel, method, options) {
  var deadline;
  var host;
  var parent;
  var propagate_flags;
  var credentials;
  if (options) {
    deadline = options.deadline;
    host = options.host;
    parent = _.get(options, 'parent.call');
    propagate_flags = options.propagate_flags;
    credentials = options.credentials;
  }
  if (deadline === undefined) {
    deadline = Infinity;
  }
  var call = new grpc.Call(channel, method, deadline, host,
                           parent, propagate_flags);
  if (credentials) {
    call.setCredentials(credentials);
  }
  return call;
}

/**
 * A generic gRPC client. Primarily useful as a base class for generated clients
 * @memberof grpc
 * @constructor
 * @param {string} address Server address to connect to
 * @param {grpc~ChannelCredentials} credentials Credentials to use to connect to
 *     the server
 * @param {Object} options Options to apply to channel creation
 */
function Client(address, credentials, options) {
  if (!options) {
    options = {};
  }
  /* Append the grpc-node user agent string after the application user agent
   * string, and put the combination at the beginning of the user agent string
   */
  if (options['grpc.primary_user_agent']) {
    options['grpc.primary_user_agent'] += ' ';
  } else {
    options['grpc.primary_user_agent'] = '';
  }
  options['grpc.primary_user_agent'] += 'grpc-node/' + version;
  /* Private fields use $ as a prefix instead of _ because it is an invalid
   * prefix of a method name */
  this.$channel = new grpc.Channel(address, credentials, options);
}

exports.Client = Client;

/**
 * @callback grpc.Client~requestCallback
 * @param {?grpc~ServiceError} error The error, if the call
 *     failed
 * @param {*} value The response value, if the call succeeded
 */

/**
 * Make a unary request to the given method, using the given serialize
 * and deserialize functions, with the given argument.
 * @param {string} method The name of the method to request
 * @param {grpc~serialize} serialize The serialization function for
 *     inputs
 * @param {grpc~deserialize} deserialize The deserialization
 *     function for outputs
 * @param {*} argument The argument to the call. Should be serializable with
 *     serialize
 * @param {grpc.Metadata=} metadata Metadata to add to the call
 * @param {grpc.Client~CallOptions=} options Options map
 * @param {grpc.Client~requestCallback} callback The callback to
 *     for when the response is received
 * @return {grpc~ClientUnaryCall} An event emitter for stream related events
 */
Client.prototype.makeUnaryRequest = function(method, serialize, deserialize,
                                             argument, metadata, options,
                                             callback) {
  /* While the arguments are listed in the function signature, those variables
   * are not used directly. Instead, ArgueJS processes the arguments
   * object. This allows for simple handling of optional arguments in the
   * middle of the argument list, and also provides type checking. */
  var args = arguejs({method: String, serialize: Function,
                      deserialize: Function,
                      argument: null, metadata: [Metadata, new Metadata()],
                      options: [Object], callback: Function}, arguments);
  var call = getCall(this.$channel, method, args.options);
  var emitter = new ClientUnaryCall(call);
  metadata = args.metadata.clone();
  var client_batch = {};
  var message = serialize(args.argument);
  if (args.options) {
    message.grpcWriteFlags = args.options.flags;
  }

  client_batch[grpc.opType.SEND_INITIAL_METADATA] =
      metadata._getCoreRepresentation();
  client_batch[grpc.opType.SEND_MESSAGE] = message;
  client_batch[grpc.opType.SEND_CLOSE_FROM_CLIENT] = true;
  client_batch[grpc.opType.RECV_INITIAL_METADATA] = true;
  client_batch[grpc.opType.RECV_MESSAGE] = true;
  client_batch[grpc.opType.RECV_STATUS_ON_CLIENT] = true;
  call.startBatch(client_batch, function(err, response) {
    response.status.metadata = Metadata._fromCoreRepresentation(
        response.status.metadata);
    var status = response.status;
    var error;
    var deserialized;
    emitter.emit('metadata', Metadata._fromCoreRepresentation(
        response.metadata));
    if (status.code === constants.status.OK) {
      if (err) {
        // Got a batch error, but OK status. Something went wrong
        args.callback(err);
        return;
      } else {
        try {
          deserialized = deserialize(response.read);
        } catch (e) {
          /* Change status to indicate bad server response. This will result
           * in passing an error to the callback */
          status = {
            code: constants.status.INTERNAL,
            details: 'Failed to parse server response'
          };
        }
      }
    }
    if (status.code !== constants.status.OK) {
      error = new Error(status.details);
      error.code = status.code;
      error.metadata = status.metadata;
      args.callback(error);
    } else {
      args.callback(null, deserialized);
    }
    emitter.emit('status', status);
  });
  return emitter;
};

/**
 * Make a client stream request to the given method, using the given serialize
 * and deserialize functions, with the given argument.
 * @param {string} method The name of the method to request
 * @param {grpc~serialize} serialize The serialization function for
 *     inputs
 * @param {grpc~deserialize} deserialize The deserialization
 *     function for outputs
 * @param {grpc.Metadata=} metadata Array of metadata key/value pairs to add to
 *     the call
 * @param {grpc.Client~CallOptions=} options Options map
 * @param {grpc.Client~requestCallback} callback The callback to for when the
 *     response is received
 * @return {grpc~ClientWritableStream} An event emitter for stream related
 *     events
 */
Client.prototype.makeClientStreamRequest = function(method, serialize,
                                                      deserialize, metadata,
                                                      options, callback) {
  /* While the arguments are listed in the function signature, those variables
   * are not used directly. Instead, ArgueJS processes the arguments
   * object. This allows for simple handling of optional arguments in the
   * middle of the argument list, and also provides type checking. */
  var args = arguejs({method:String, serialize: Function,
                      deserialize: Function,
                      metadata: [Metadata, new Metadata()],
                      options: [Object], callback: Function}, arguments);
  var call = getCall(this.$channel, method, args.options);
  metadata = args.metadata.clone();
  var stream = new ClientWritableStream(call, serialize);
  var metadata_batch = {};
  metadata_batch[grpc.opType.SEND_INITIAL_METADATA] =
      metadata._getCoreRepresentation();
  metadata_batch[grpc.opType.RECV_INITIAL_METADATA] = true;
  call.startBatch(metadata_batch, function(err, response) {
    if (err) {
      // The call has stopped for some reason. A non-OK status will arrive
      // in the other batch.
      return;
    }
    stream.emit('metadata', Metadata._fromCoreRepresentation(
        response.metadata));
  });
  var client_batch = {};
  client_batch[grpc.opType.RECV_MESSAGE] = true;
  client_batch[grpc.opType.RECV_STATUS_ON_CLIENT] = true;
  call.startBatch(client_batch, function(err, response) {
    response.status.metadata = Metadata._fromCoreRepresentation(
        response.status.metadata);
    var status = response.status;
    var error;
    var deserialized;
    if (status.code === constants.status.OK) {
      if (err) {
        // Got a batch error, but OK status. Something went wrong
        args.callback(err);
        return;
      } else {
        try {
          deserialized = deserialize(response.read);
        } catch (e) {
          /* Change status to indicate bad server response. This will result
           * in passing an error to the callback */
          status = {
            code: constants.status.INTERNAL,
            details: 'Failed to parse server response'
          };
        }
      }
    }
    if (status.code !== constants.status.OK) {
      error = new Error(response.status.details);
      error.code = status.code;
      error.metadata = status.metadata;
      args.callback(error);
    } else {
      args.callback(null, deserialized);
    }
    stream.emit('status', status);
  });
  return stream;
};

/**
 * Make a server stream request to the given method, with the given serialize
 * and deserialize function, using the given argument
 * @param {string} method The name of the method to request
 * @param {grpc~serialize} serialize The serialization function for inputs
 * @param {grpc~deserialize} deserialize The deserialization
 *     function for outputs
 * @param {*} argument The argument to the call. Should be serializable with
 *     serialize
 * @param {grpc.Metadata=} metadata Array of metadata key/value pairs to add to
 *     the call
 * @param {grpc.Client~CallOptions=} options Options map
 * @return {grpc~ClientReadableStream} An event emitter for stream related
 *     events
 */
Client.prototype.makeServerStreamRequest = function(method, serialize,
                                                    deserialize, argument,
                                                    metadata, options) {
  /* While the arguments are listed in the function signature, those variables
   * are not used directly. Instead, ArgueJS processes the arguments
   * object. */
  var args = arguejs({method:String, serialize: Function,
                      deserialize: Function,
                      argument: null, metadata: [Metadata, new Metadata()],
                      options: [Object]}, arguments);
  var call = getCall(this.$channel, method, args.options);
  metadata = args.metadata.clone();
  var stream = new ClientReadableStream(call, deserialize);
  var start_batch = {};
  var message = serialize(args.argument);
  if (args.options) {
    message.grpcWriteFlags = args.options.flags;
  }
  start_batch[grpc.opType.SEND_INITIAL_METADATA] =
      metadata._getCoreRepresentation();
  start_batch[grpc.opType.RECV_INITIAL_METADATA] = true;
  start_batch[grpc.opType.SEND_MESSAGE] = message;
  start_batch[grpc.opType.SEND_CLOSE_FROM_CLIENT] = true;
  call.startBatch(start_batch, function(err, response) {
    if (err) {
      // The call has stopped for some reason. A non-OK status will arrive
      // in the other batch.
      return;
    }
    stream.emit('metadata', Metadata._fromCoreRepresentation(
        response.metadata));
  });
  var status_batch = {};
  status_batch[grpc.opType.RECV_STATUS_ON_CLIENT] = true;
  call.startBatch(status_batch, function(err, response) {
    if (err) {
      stream.emit('error', err);
      return;
    }
    response.status.metadata = Metadata._fromCoreRepresentation(
        response.status.metadata);
    stream._receiveStatus(response.status);
  });
  return stream;
};


/**
 * Make a bidirectional stream request with this method on the given channel.
 * @param {string} method The name of the method to request
 * @param {grpc~serialize} serialize The serialization function for inputs
 * @param {grpc~deserialize} deserialize The deserialization
 *     function for outputs
 * @param {grpc.Metadata=} metadata Array of metadata key/value
 *     pairs to add to the call
 * @param {grpc.Client~CallOptions=} options Options map
 * @return {grpc~ClientDuplexStream} An event emitter for stream related events
 */
Client.prototype.makeBidiStreamRequest = function(method, serialize,
                                                  deserialize, metadata,
                                                  options) {
  /* While the arguments are listed in the function signature, those variables
   * are not used directly. Instead, ArgueJS processes the arguments
   * object. */
  var args = arguejs({method:String, serialize: Function,
                      deserialize: Function,
                      metadata: [Metadata, new Metadata()],
                      options: [Object]}, arguments);
  var call = getCall(this.$channel, method, args.options);
  metadata = args.metadata.clone();
  var stream = new ClientDuplexStream(call, serialize, deserialize);
  var start_batch = {};
  start_batch[grpc.opType.SEND_INITIAL_METADATA] =
      metadata._getCoreRepresentation();
  start_batch[grpc.opType.RECV_INITIAL_METADATA] = true;
  call.startBatch(start_batch, function(err, response) {
    if (err) {
      // The call has stopped for some reason. A non-OK status will arrive
      // in the other batch.
      return;
    }
    stream.emit('metadata', Metadata._fromCoreRepresentation(
        response.metadata));
  });
  var status_batch = {};
  status_batch[grpc.opType.RECV_STATUS_ON_CLIENT] = true;
  call.startBatch(status_batch, function(err, response) {
    if (err) {
      stream.emit('error', err);
      return;
    }
    response.status.metadata = Metadata._fromCoreRepresentation(
        response.status.metadata);
    stream._receiveStatus(response.status);
  });
  return stream;
};

/**
 * Close this client.
 */
Client.prototype.close = function() {
  this.$channel.close();
};

/**
 * Return the underlying channel object for the specified client
 * @return {Channel} The channel
 */
Client.prototype.getChannel = function() {
  return this.$channel;
};

/**
 * Wait for the client to be ready. The callback will be called when the
 * client has successfully connected to the server, and it will be called
 * with an error if the attempt to connect to the server has unrecoverablly
 * failed or if the deadline expires. This function will make the channel
 * start connecting if it has not already done so.
 * @param {grpc~Deadline} deadline When to stop waiting for a connection.
 * @param {function(Error)} callback The callback to call when done attempting
 *     to connect.
 */
Client.prototype.waitForReady = function(deadline, callback) {
  var self = this;
  var checkState = function(err) {
    if (err) {
      callback(new Error('Failed to connect before the deadline'));
      return;
    }
    var new_state = self.$channel.getConnectivityState(true);
    if (new_state === grpc.connectivityState.READY) {
      callback();
    } else if (new_state === grpc.connectivityState.FATAL_FAILURE) {
      callback(new Error('Failed to connect to server'));
    } else {
      self.$channel.watchConnectivityState(new_state, deadline, checkState);
    }
  };
  checkState();
};

/**
 * Map with short names for each of the requester maker functions. Used in
 * makeClientConstructor
 * @private
 */
var requester_funcs = {
  unary: Client.prototype.makeUnaryRequest,
  server_stream: Client.prototype.makeServerStreamRequest,
  client_stream: Client.prototype.makeClientStreamRequest,
  bidi: Client.prototype.makeBidiStreamRequest
};

function getDefaultValues(metadata, options) {
  var res = {};
  res.metadata = metadata || new Metadata();
  res.options = options || {};
  return res;
}

/**
 * Map with wrappers for each type of requester function to make it use the old
 * argument order with optional arguments after the callback.
 * @access private
 */
var deprecated_request_wrap = {
  unary: function(makeUnaryRequest) {
    return function makeWrappedUnaryRequest(argument, callback,
                                            metadata, options) {
      /* jshint validthis: true */
      var opt_args = getDefaultValues(metadata, metadata);
      return makeUnaryRequest.call(this, argument, opt_args.metadata,
                                   opt_args.options, callback);
    };
  },
  client_stream: function(makeServerStreamRequest) {
    return function makeWrappedClientStreamRequest(callback, metadata,
                                                   options) {
      /* jshint validthis: true */
      var opt_args = getDefaultValues(metadata, options);
      return makeServerStreamRequest.call(this, opt_args.metadata,
                                          opt_args.options, callback);
    };
  },
  server_stream: _.identity,
  bidi: _.identity
};

/**
 * Creates a constructor for a client with the given methods, as specified in
 * the methods argument. The resulting class will have an instance method for
 * each method in the service, which is a partial application of one of the
 * [Client]{@link grpc.Client} request methods, depending on `requestSerialize`
 * and `responseSerialize`, with the `method`, `serialize`, and `deserialize`
 * arguments predefined.
 * @memberof grpc
 * @alias grpc~makeGenericClientConstructor
 * @param {grpc~ServiceDefinition} methods An object mapping method names to
 *     method attributes
 * @param {string} serviceName The fully qualified name of the service
 * @param {Object} class_options An options object.
 * @param {boolean=} [class_options.deprecatedArgumentOrder=false] Indicates
 *     that the old argument order should be used for methods, with optional
 *     arguments at the end instead of the callback at the end. This option
 *     is only a temporary stopgap measure to smooth an API breakage.
 *     It is deprecated, and new code should not use it.
 * @return {function} New client constructor, which is a subclass of
 *     {@link grpc.Client}, and has the same arguments as that constructor.
 */
exports.makeClientConstructor = function(methods, serviceName,
                                         class_options) {
  if (!class_options) {
    class_options = {};
  }

  function ServiceClient(address, credentials, options) {
    Client.call(this, address, credentials, options);
  }

  util.inherits(ServiceClient, Client);

  _.each(methods, function(attrs, name) {
    var method_type;
    if (_.startsWith(name, '$')) {
      throw new Error('Method names cannot start with $');
    }
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
    var serialize = attrs.requestSerialize;
    var deserialize = attrs.responseDeserialize;
    var method_func = _.partial(requester_funcs[method_type], attrs.path,
                                serialize, deserialize);
    if (class_options.deprecatedArgumentOrder) {
      ServiceClient.prototype[name] = deprecated_request_wrap(method_func);
    } else {
      ServiceClient.prototype[name] = method_func;
    }
    // Associate all provided attributes with the method
    _.assign(ServiceClient.prototype[name], attrs);
  });

  ServiceClient.service = methods;

  return ServiceClient;
};

/**
 * Return the underlying channel object for the specified client
 * @memberof grpc
 * @alias grpc~getClientChannel
 * @param {Client} client
 * @return {Channel} The channel
 * @see grpc.Client#getChannel
 */
exports.getClientChannel = function(client) {
  return Client.prototype.getChannel.call(client);
};

/**
 * Wait for the client to be ready. The callback will be called when the
 * client has successfully connected to the server, and it will be called
 * with an error if the attempt to connect to the server has unrecoverablly
 * failed or if the deadline expires. This function will make the channel
 * start connecting if it has not already done so.
 * @memberof grpc
 * @alias grpc~waitForClientReady
 * @param {Client} client The client to wait on
 * @param {grpc~Deadline} deadline When to stop waiting for a connection. Pass
 *     Infinity to wait forever.
 * @param {function(Error)} callback The callback to call when done attempting
 *     to connect.
 * @see grpc.Client#waitForReady
 */
exports.waitForClientReady = function(client, deadline, callback) {
  Client.prototype.waitForReady.call(client, deadline, callback);
};
