/*
 *
 * Copyright 2014, Google Inc.
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

var grpc = require('bindings')('grpc.node');

var common = require('./common');

var Duplex = require('stream').Duplex;
var util = require('util');

util.inherits(GrpcServerStream, Duplex);

/**
 * Class for representing a gRPC server side stream as a Node stream. Extends
 * from stream.Duplex.
 * @constructor
 * @param {grpc.Call} call Call object to proxy
 * @param {function(*):Buffer=} serialize Serialization function for responses
 * @param {function(Buffer):*=} deserialize Deserialization function for
 *     requests
 */
function GrpcServerStream(call, serialize, deserialize) {
  Duplex.call(this, {objectMode: true});
  if (!serialize) {
    serialize = function(value) {
      return value;
    };
  }
  if (!deserialize) {
    deserialize = function(value) {
      return value;
    };
  }
  this._call = call;
  // Indicate that a status has been sent
  var finished = false;
  var self = this;
  var status = {
    'code' : grpc.status.OK,
    'details' : 'OK'
  };

  /**
   * Serialize a response value to a buffer. Always maps null to null. Otherwise
   * uses the provided serialize function
   * @param {*} value The value to serialize
   * @return {Buffer} The serialized value
   */
  this.serialize = function(value) {
    if (value === null || value === undefined) {
      return null;
    }
    return serialize(value);
  };

  /**
   * Deserialize a request buffer to a value. Always maps null to null.
   * Otherwise uses the provided deserialize function.
   * @param {Buffer} buffer The buffer to deserialize
   * @return {*} The deserialized value
   */
  this.deserialize = function(buffer) {
    if (buffer === null) {
      return null;
    }
    return deserialize(buffer);
  };

  /**
   * Send the pending status
   */
  function sendStatus() {
    call.startWriteStatus(status.code, status.details, function() {
    });
    finished = true;
  }
  this.on('finish', sendStatus);
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
    status = {'code': code, 'details': details};
  }
  /**
   * Terminate the call. This includes indicating that reads are done, draining
   * all pending writes, and sending the given error as a status
   * @param {Error} err The error object
   * @this GrpcServerStream
   */
  function terminateCall(err) {
    // Drain readable data
    this.on('data', function() {});
    setStatus(err);
    this.end();
  }
  this.on('error', terminateCall);
  // Indicates that a read is pending
  var reading = false;
  /**
   * Callback to be called when a READ event is received. Pushes the data onto
   * the read queue and starts reading again if applicable
   * @param {grpc.Event} event READ event object
   */
  function readCallback(event) {
    if (finished) {
      self.push(null);
      return;
    }
    var data = event.data;
    if (self.push(deserialize(data)) && data != null) {
      self._call.startRead(readCallback);
    } else {
      reading = false;
    }
  }
  /**
   * Start reading if there is not already a pending read. Reading will
   * continue until self.push returns false (indicating reads should slow
   * down) or the read data is null (indicating that there is no more data).
   */
  this.startReading = function() {
    if (finished) {
      self.push(null);
    } else {
      if (!reading) {
        reading = true;
        self._call.startRead(readCallback);
      }
    }
  };
}

/**
 * Start reading from the gRPC data source. This is an implementation of a
 * method required for implementing stream.Readable
 * @param {number} size Ignored
 */
GrpcServerStream.prototype._read = function(size) {
  this.startReading();
};

/**
 * Start writing a chunk of data. This is an implementation of a method required
 * for implementing stream.Writable.
 * @param {Buffer} chunk The chunk of data to write
 * @param {string} encoding Ignored
 * @param {function(Error=)} callback Callback to indicate that the write is
 *     complete
 */
GrpcServerStream.prototype._write = function(chunk, encoding, callback) {
  var self = this;
  self._call.startWrite(self.serialize(chunk), function(event) {
    callback();
  }, 0);
};

/**
 * Constructs a server object that stores request handlers and delegates
 * incoming requests to those handlers
 * @constructor
 * @param {Array} options Options that should be passed to the internal server
 *     implementation
 */
function Server(options) {
  this.handlers = {};
  var handlers = this.handlers;
  var server = new grpc.Server(options);
  this._server = server;
  var started = false;
  /**
   * Start the server and begin handling requests
   * @this Server
   */
  this.start = function() {
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
    function handleNewCall(event) {
      var call = event.call;
      var data = event.data;
      if (data == null) {
        return;
      }
      server.requestCall(handleNewCall);
      var handler = undefined;
      var deadline = data.absolute_deadline;
      var cancelled = false;
      if (handlers.hasOwnProperty(data.method)) {
        handler = handlers[data.method];
      }
      call.serverAccept(function(event) {
        if (event.data.code === grpc.status.CANCELLED) {
          cancelled = true;
        }
      }, 0);
      call.serverEndInitialMetadata(0);
      var stream = new GrpcServerStream(call, handler.serialize,
                                        handler.deserialize);
      Object.defineProperty(stream, 'cancelled', {
        get: function() { return cancelled;}
      });
      try {
        handler.func(stream, data.metadata);
      } catch (e) {
        stream.emit('error', e);
      }
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
 * @return {boolean} True if the handler was set. False if a handler was already
 *     set for that name.
 */
Server.prototype.register = function(name, handler, serialize, deserialize) {
  if (this.handlers.hasOwnProperty(name)) {
    return false;
  }
  this.handlers[name] = {
    func: handler,
    serialize: serialize,
    deserialize: deserialize
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
 * See documentation for Server
 */
module.exports = Server;
