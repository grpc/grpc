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

var common = require('./common.js');

var EventEmitter = require('events').EventEmitter;

var stream = require('stream');

var Readable = stream.Readable;
var Writable = stream.Writable;
var Duplex = stream.Duplex;
var util = require('util');
var version = require('../package.json').version;

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
  /* jshint validthis: true */
  var batch = {};
  batch[grpc.opType.SEND_MESSAGE] = this.serialize(chunk);
  this.call.startBatch(batch, function(err, event) {
    if (err) {
      // Something has gone wrong. Stop writing by failing to call callback
      return;
    }
    callback();
  });
}

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
      return;
    }
    var data = event.read;
    if (self.push(self.deserialize(data)) && data !== null) {
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
  /* jshint validthis: true */
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
    /* jshint validthis: true */
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
    this.updateMetadata(this.auth_uri, metadata, function(error, metadata) {
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
        emitter.emit('status', response.status);
        if (response.status.code !== grpc.status.OK) {
          var error = new Error(response.status.details);
          error.code = response.status.code;
          error.metadata = response.status.metadata;
          callback(error);
          return;
        } else {
          if (err) {
            // Got a batch error, but OK status. Something went wrong
            callback(err);
            return;
          }
        }
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
    /* jshint validthis: true */
    if (deadline === undefined) {
      deadline = Infinity;
    }
    var call = new grpc.Call(this.channel, method, deadline);
    if (metadata === null || metadata === undefined) {
      metadata = {};
    }
    var stream = new ClientWritableStream(call, serialize);
    this.updateMetadata(this.auth_uri, metadata, function(error, metadata) {
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
          // The call has stopped for some reason. A non-OK status will arrive
          // in the other batch.
          return;
        }
        stream.emit('metadata', response.metadata);
      });
      var client_batch = {};
      client_batch[grpc.opType.RECV_MESSAGE] = true;
      client_batch[grpc.opType.RECV_STATUS_ON_CLIENT] = true;
      call.startBatch(client_batch, function(err, response) {
        stream.emit('status', response.status);
        if (response.status.code !== grpc.status.OK) {
          var error = new Error(response.status.details);
          error.code = response.status.code;
          error.metadata = response.status.metadata;
          callback(error);
          return;
        } else {
          if (err) {
            // Got a batch error, but OK status. Something went wrong
            callback(err);
            return;
          }
        }
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
    /* jshint validthis: true */
    if (deadline === undefined) {
      deadline = Infinity;
    }
    var call = new grpc.Call(this.channel, method, deadline);
    if (metadata === null || metadata === undefined) {
      metadata = {};
    }
    var stream = new ClientReadableStream(call, deserialize);
    this.updateMetadata(this.auth_uri, metadata, function(error, metadata) {
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
          // The call has stopped for some reason. A non-OK status will arrive
          // in the other batch.
          return;
        }
        stream.emit('metadata', response.metadata);
      });
      var status_batch = {};
      status_batch[grpc.opType.RECV_STATUS_ON_CLIENT] = true;
      call.startBatch(status_batch, function(err, response) {
        stream.emit('status', response.status);
        if (response.status.code !== grpc.status.OK) {
          var error = new Error(response.status.details);
          error.code = response.status.code;
          error.metadata = response.status.metadata;
          stream.emit('error', error);
          return;
        } else {
          if (err) {
            // Got a batch error, but OK status. Something went wrong
            stream.emit('error', err);
            return;
          }
        }
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
    /* jshint validthis: true */
    if (deadline === undefined) {
      deadline = Infinity;
    }
    var call = new grpc.Call(this.channel, method, deadline);
    if (metadata === null || metadata === undefined) {
      metadata = {};
    }
    var stream = new ClientDuplexStream(call, serialize, deserialize);
    this.updateMetadata(this.auth_uri, metadata, function(error, metadata) {
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
          // The call has stopped for some reason. A non-OK status will arrive
          // in the other batch.
          return;
        }
        stream.emit('metadata', response.metadata);
      });
      var status_batch = {};
      status_batch[grpc.opType.RECV_STATUS_ON_CLIENT] = true;
      call.startBatch(status_batch, function(err, response) {
        stream.emit('status', response.status);
        if (response.status.code !== grpc.status.OK) {
          var error = new Error(response.status.details);
          error.code = response.status.code;
          error.metadata = response.status.metadata;
          stream.emit('error', error);
          return;
        } else {
          if (err) {
            // Got a batch error, but OK status. Something went wrong
            stream.emit('error', err);
            return;
          }
        }
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
 * Creates a constructor for a client with the given methods. The methods object
 * maps method name to an object with the following keys:
 * path: The path on the server for accessing the method. For example, for
 *     protocol buffers, we use "/service_name/method_name"
 * requestStream: bool indicating whether the client sends a stream
 * resonseStream: bool indicating whether the server sends a stream
 * requestSerialize: function to serialize request objects
 * responseDeserialize: function to deserialize response objects
 * @param {Object} methods An object mapping method names to method attributes
 * @param {string} serviceName The name of the service
 * @return {function(string, Object)} New client constructor
 */
function makeClientConstructor(methods, serviceName) {
  /**
   * Create a client with the given methods
   * @constructor
   * @param {string} address The address of the server to connect to
   * @param {Object} options Options to pass to the underlying channel
   * @param {function(string, Object, function)=} updateMetadata function to
   *     update the metadata for each request
   */
  function Client(address, options, updateMetadata) {
    if (!updateMetadata) {
      updateMetadata = function(uri, metadata, callback) {
        callback(null, metadata);
      };
    }
    if (!options) {
      options = {};
    }
    options['grpc.primary_user_agent'] = 'grpc-node/' + version;
    this.channel = new grpc.Channel(address, options);
    this.server_address = address.replace(/\/$/, '');
    this.auth_uri = this.server_address + '/' + serviceName;
    this.updateMetadata = updateMetadata;
  }

  _.each(methods, function(attrs, name) {
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
    var serialize = attrs.requestSerialize;
    var deserialize = attrs.responseDeserialize;
    Client.prototype[name] = requester_makers[method_type](
        attrs.path, serialize, deserialize);
    Client.prototype[name].serialize = serialize;
    Client.prototype[name].deserialize = deserialize;
  });

  return Client;
}

/**
 * Creates a constructor for clients for the given service
 * @param {ProtoBuf.Reflect.Service} service The service to generate a client
 *     for
 * @return {function(string, Object)} New client constructor
 */
function makeProtobufClientConstructor(service) {
  var method_attrs = common.getProtobufServiceAttrs(service, service.name);
  var Client = makeClientConstructor(method_attrs);
  Client.service = service;

  return Client;
}

exports.makeClientConstructor = makeClientConstructor;

exports.makeProtobufClientConstructor = makeProtobufClientConstructor;

/**
 * See docs for client.status
 */
exports.status = grpc.status;
/**
 * See docs for client.callError
 */
exports.callError = grpc.callError;
