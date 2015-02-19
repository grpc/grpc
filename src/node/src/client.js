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

var common = require('./common.js');

var EventEmitter = require('events').EventEmitter;

var stream = require('stream');

var Readable = stream.Readable;
var Writable = stream.Writable;
var Duplex = stream.Duplex;
var util = require('util');

util.inherits(ClientWritableStream, Writable);

/**
 * A stream that the client can write to. Used for calls that are streaming from
 * the client side.
 * @constructor
 * @param {grpc.Call} call The call object to send data with
 * @param {function(*):Buffer=} serialize Serialization function for writes.
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
 * Attempt to write the given chunk. Calls the callback when done. This is an
 * implementation of a method needed for implementing stream.Writable.
 * @param {Buffer} chunk The chunk to write
 * @param {string} encoding Ignored
 * @param {function(Error=)} callback Called when the write is complete
 */
function _write(chunk, encoding, callback) {
  var batch = {};
  batch[grpc.opType.SEND_MESSAGE] = this.serialize(chunk);
  this.call.startBatch(batch, function(err, event) {
    if (err) {
      throw err;
    }
    callback();
  });
};

ClientWritableStream.prototype._write = _write;

util.inherits(ClientReadableStream, Readable);

/**
 * A stream that the client can read from. Used for calls that are streaming
 * from the server side.
 * @constructor
 * @param {grpc.Call} call The call object to read data with
 * @param {function(Buffer):*=} deserialize Deserialization function for reads
 */
function ClientReadableStream(call, deserialize) {
  Readable.call(this, {objectMode: true});
  this.call = call;
  this.finished = false;
  this.reading = false;
  this.deserialize = common.wrapIgnoreNull(deserialize);
}

/**
 * Read the next object from the stream.
 * @param {*} size Ignored because we use objectMode=true
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
      throw err;
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
      var read_batch = {};
      read_batch[grpc.opType.RECV_MESSAGE] = true;
      self.call.startBatch(read_batch, readCallback);
    }
  }
};

ClientReadableStream.prototype._read = _read;

util.inherits(ClientDuplexStream, Duplex);

/**
 * A stream that the client can read from or write to. Used for calls with
 * duplex streaming.
 * @constructor
 * @param {grpc.Call} call Call object to proxy
 * @param {function(*):Buffer=} serialize Serialization function for requests
 * @param {function(Buffer):*=} deserialize Deserialization function for
 *     responses
 */
function ClientDuplexStream(call, serialize, deserialize) {
  Duplex.call(this, {objectMode: true});
  this.serialize = common.wrapIgnoreNull(serialize);
  this.deserialize = common.wrapIgnoreNull(deserialize);
  var self = this;
  var finished = false;
  // Indicates that a read is currently pending
  var reading = false;
  this.call = call;
  this.on('finish', function() {
    var batch = {};
    batch[grpc.opType.SEND_CLOSE_FROM_CLIENT] = true;
    call.startBatch(batch, function() {});
  });
}

ClientDuplexStream.prototype._read = _read;
ClientDuplexStream.prototype._write = _write;

/**
 * Cancel the ongoing call
 */
function cancel() {
  this.call.cancel();
}

ClientReadableStream.prototype.cancel = cancel;
ClientWritableStream.prototype.cancel = cancel;
ClientDuplexStream.prototype.cancel = cancel;

/**
 * Get a function that can make unary requests to the specified method.
 * @param {string} method The name of the method to request
 * @param {function(*):Buffer} serialize The serialization function for inputs
 * @param {function(Buffer)} deserialize The deserialization function for
 *     outputs
 * @return {Function} makeUnaryRequest
 */
function makeUnaryRequestFunction(method, serialize, deserialize) {
  /**
   * Make a unary request with this method on the given channel with the given
   * argument, callback, etc.
   * @this {Client} Client object. Must have a channel member.
   * @param {*} argument The argument to the call. Should be serializable with
   *     serialize
   * @param {function(?Error, value=)} callback The callback to for when the
   *     response is received
   * @param {array=} metadata Array of metadata key/value pairs to add to the
   *     call
   * @param {(number|Date)=} deadline The deadline for processing this request.
   *     Defaults to infinite future
   * @return {EventEmitter} An event emitter for stream related events
   */
  function makeUnaryRequest(argument, callback, metadata, deadline) {
    if (deadline === undefined) {
      deadline = Infinity;
    }
    var emitter = new EventEmitter();
    var call = new grpc.Call(this.channel, method, deadline);
    if (metadata === null || metadata === undefined) {
      metadata = {};
    }
    emitter.cancel = function cancel() {
      call.cancel();
    };
    this.updateMetadata(metadata, function(error, metadata) {
      if (error) {
        call.cancel();
        callback(error);
        return;
      }
      var client_batch = {};
      client_batch[grpc.opType.SEND_INITIAL_METADATA] = metadata;
      client_batch[grpc.opType.SEND_MESSAGE] = serialize(argument);
      client_batch[grpc.opType.SEND_CLOSE_FROM_CLIENT] = true;
      client_batch[grpc.opType.RECV_INITIAL_METADATA] = true;
      client_batch[grpc.opType.RECV_MESSAGE] = true;
      client_batch[grpc.opType.RECV_STATUS_ON_CLIENT] = true;
      call.startBatch(client_batch, function(err, response) {
        if (err) {
          callback(err);
          return;
        }
        if (response.status.code != grpc.status.OK) {
          callback(response.status);
          return;
        }
        emitter.emit('status', response.status);
        emitter.emit('metadata', response.metadata);
        callback(null, deserialize(response.read));
      });
    });
    return emitter;
  }
  return makeUnaryRequest;
}

/**
 * Get a function that can make client stream requests to the specified method.
 * @param {string} method The name of the method to request
 * @param {function(*):Buffer} serialize The serialization function for inputs
 * @param {function(Buffer)} deserialize The deserialization function for
 *     outputs
 * @return {Function} makeClientStreamRequest
 */
function makeClientStreamRequestFunction(method, serialize, deserialize) {
  /**
   * Make a client stream request with this method on the given channel with the
   * given callback, etc.
   * @this {Client} Client object. Must have a channel member.
   * @param {function(?Error, value=)} callback The callback to for when the
   *     response is received
   * @param {array=} metadata Array of metadata key/value pairs to add to the
   *     call
   * @param {(number|Date)=} deadline The deadline for processing this request.
   *     Defaults to infinite future
   * @return {EventEmitter} An event emitter for stream related events
   */
  function makeClientStreamRequest(callback, metadata, deadline) {
    if (deadline === undefined) {
      deadline = Infinity;
    }
    var call = new grpc.Call(this.channel, method, deadline);
    if (metadata === null || metadata === undefined) {
      metadata = {};
    }
    var stream = new ClientWritableStream(call, serialize);
    this.updateMetadata(metadata, function(error, metadata) {
      if (error) {
        call.cancel();
        callback(error);
        return;
      }
      var metadata_batch = {};
      metadata_batch[grpc.opType.SEND_INITIAL_METADATA] = metadata;
      metadata_batch[grpc.opType.RECV_INITIAL_METADATA] = true;
      call.startBatch(metadata_batch, function(err, response) {
        if (err) {
          callback(err);
          return;
        }
        stream.emit('metadata', response.metadata);
      });
      var client_batch = {};
      client_batch[grpc.opType.RECV_MESSAGE] = true;
      client_batch[grpc.opType.RECV_STATUS_ON_CLIENT] = true;
      call.startBatch(client_batch, function(err, response) {
        if (err) {
          callback(err);
          return;
        }
        if (response.status.code != grpc.status.OK) {
          callback(response.status);
          return;
        }
        stream.emit('status', response.status);
        callback(null, deserialize(response.read));
      });
    });
    return stream;
  }
  return makeClientStreamRequest;
}

/**
 * Get a function that can make server stream requests to the specified method.
 * @param {string} method The name of the method to request
 * @param {function(*):Buffer} serialize The serialization function for inputs
 * @param {function(Buffer)} deserialize The deserialization function for
 *     outputs
 * @return {Function} makeServerStreamRequest
 */
function makeServerStreamRequestFunction(method, serialize, deserialize) {
  /**
   * Make a server stream request with this method on the given channel with the
   * given argument, etc.
   * @this {SurfaceClient} Client object. Must have a channel member.
   * @param {*} argument The argument to the call. Should be serializable with
   *     serialize
   * @param {array=} metadata Array of metadata key/value pairs to add to the
   *     call
   * @param {(number|Date)=} deadline The deadline for processing this request.
   *     Defaults to infinite future
   * @return {EventEmitter} An event emitter for stream related events
   */
  function makeServerStreamRequest(argument, metadata, deadline) {
    if (deadline === undefined) {
      deadline = Infinity;
    }
    var call = new grpc.Call(this.channel, method, deadline);
    if (metadata === null || metadata === undefined) {
      metadata = {};
    }
    var stream = new ClientReadableStream(call, deserialize);
    this.updateMetadata(metadata, function(error, metadata) {
      if (error) {
        call.cancel();
        stream.emit('error', error);
        return;
      }
      var start_batch = {};
      start_batch[grpc.opType.SEND_INITIAL_METADATA] = metadata;
      start_batch[grpc.opType.RECV_INITIAL_METADATA] = true;
      start_batch[grpc.opType.SEND_MESSAGE] = serialize(argument);
      start_batch[grpc.opType.SEND_CLOSE_FROM_CLIENT] = true;
      call.startBatch(start_batch, function(err, response) {
        if (err) {
          throw err;
        }
        stream.emit('metadata', response.metadata);
      });
      var status_batch = {};
      status_batch[grpc.opType.RECV_STATUS_ON_CLIENT] = true;
      call.startBatch(status_batch, function(err, response) {
        if (err) {
          throw err;
        }
        stream.emit('status', response.status);
      });
    });
    return stream;
  }
  return makeServerStreamRequest;
}

/**
 * Get a function that can make bidirectional stream requests to the specified
 * method.
 * @param {string} method The name of the method to request
 * @param {function(*):Buffer} serialize The serialization function for inputs
 * @param {function(Buffer)} deserialize The deserialization function for
 *     outputs
 * @return {Function} makeBidiStreamRequest
 */
function makeBidiStreamRequestFunction(method, serialize, deserialize) {
  /**
   * Make a bidirectional stream request with this method on the given channel.
   * @this {SurfaceClient} Client object. Must have a channel member.
   * @param {array=} metadata Array of metadata key/value pairs to add to the
   *     call
   * @param {(number|Date)=} deadline The deadline for processing this request.
   *     Defaults to infinite future
   * @return {EventEmitter} An event emitter for stream related events
   */
  function makeBidiStreamRequest(metadata, deadline) {
    if (deadline === undefined) {
      deadline = Infinity;
    }
    var call = new grpc.Call(this.channel, method, deadline);
    if (metadata === null || metadata === undefined) {
      metadata = {};
    }
    var stream = new ClientDuplexStream(call, serialize, deserialize);
    this.updateMetadata(metadata, function(error, metadata) {
      if (error) {
        call.cancel();
        stream.emit('error', error);
        return;
      }
      var start_batch = {};
      start_batch[grpc.opType.SEND_INITIAL_METADATA] = metadata;
      start_batch[grpc.opType.RECV_INITIAL_METADATA] = true;
      call.startBatch(start_batch, function(err, response) {
        if (err) {
          throw err;
        }
        stream.emit('metadata', response.metadata);
      });
      var status_batch = {};
      status_batch[grpc.opType.RECV_STATUS_ON_CLIENT] = true;
      call.startBatch(status_batch, function(err, response) {
        if (err) {
          throw err;
        }
        stream.emit('status', response.status);
      });
    });
    return stream;
  }
  return makeBidiStreamRequest;
}


/**
 * Map with short names for each of the requester maker functions. Used in
 * makeClientConstructor
 */
var requester_makers = {
  unary: makeUnaryRequestFunction,
  server_stream: makeServerStreamRequestFunction,
  client_stream: makeClientStreamRequestFunction,
  bidi: makeBidiStreamRequestFunction
};

/**
 * Creates a constructor for clients for the given service
 * @param {ProtoBuf.Reflect.Service} service The service to generate a client
 *     for
 * @return {function(string, Object)} New client constructor
 */
function makeClientConstructor(service) {
  var prefix = '/' + common.fullyQualifiedName(service) + '/';
  /**
   * Create a client with the given methods
   * @constructor
   * @param {string} address The address of the server to connect to
   * @param {Object} options Options to pass to the underlying channel
   * @param {function(Object, function)=} updateMetadata function to update the
   *     metadata for each request
   */
  function Client(address, options, updateMetadata) {
    if (updateMetadata) {
      this.updateMetadata = updateMetadata;
    } else {
      this.updateMetadata = function(metadata, callback) {
        callback(null, metadata);
      };
    }
    this.channel = new grpc.Channel(address, options);
  }

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
    var serialize = common.serializeCls(method.resolvedRequestType.build());
    var deserialize = common.deserializeCls(
        method.resolvedResponseType.build());
    Client.prototype[decapitalize(method.name)] = requester_makers[method_type](
        prefix + capitalize(method.name), serialize, deserialize);
    Client.prototype[decapitalize(method.name)].serialize = serialize;
    Client.prototype[decapitalize(method.name)].deserialize = deserialize;
  });

  Client.service = service;

  return Client;
}

exports.makeClientConstructor = makeClientConstructor;

/**
 * See docs for client.status
 */
exports.status = grpc.status;
/**
 * See docs for client.callError
 */
exports.callError = grpc.callError;
