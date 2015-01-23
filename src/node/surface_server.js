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

var capitalize = require('underscore.string/capitalize');
var decapitalize = require('underscore.string/decapitalize');

var Server = require('./server.js');

var stream = require('stream');

var Readable = stream.Readable;
var Writable = stream.Writable;
var Duplex = stream.Duplex;
var util = require('util');

var common = require('./common.js');

util.inherits(ServerReadableObjectStream, Readable);

/**
 * Class for representing a gRPC client streaming call as a Node stream on the
 * server side. Extends from stream.Readable.
 * @constructor
 * @param {stream} stream Underlying binary Duplex stream for the call
 */
function ServerReadableObjectStream(stream) {
  var options = {objectMode: true};
  Readable.call(this, options);
  this._stream = stream;
  Object.defineProperty(this, 'cancelled', {
    get: function() { return stream.cancelled; }
  });
  var self = this;
  this._stream.on('data', function forwardData(chunk) {
    if (!self.push(chunk)) {
      self._stream.pause();
    }
  });
  this._stream.on('end', function forwardEnd() {
    self.push(null);
  });
  this._stream.pause();
}

util.inherits(ServerWritableObjectStream, Writable);

/**
 * Class for representing a gRPC server streaming call as a Node stream on the
 * server side. Extends from stream.Writable.
 * @constructor
 * @param {stream} stream Underlying binary Duplex stream for the call
 */
function ServerWritableObjectStream(stream) {
  var options = {objectMode: true};
  Writable.call(this, options);
  this._stream = stream;
  this.on('finish', function() {
    this._stream.end();
  });
}
/**
 * _read implementation for both types of streams that allow reading.
 * @this {ServerReadableObjectStream|ServerBidiObjectStream}
 * @param {number} size Ignored
 */
function _read(size) {
  this._stream.resume();
}

/**
 * See docs for _read
 */
ServerReadableObjectStream.prototype._read = _read;

/**
 * _write implementation for both types of streams that allow writing
 * @this {ServerWritableObjectStream}
 * @param {*} chunk The value to write to the stream
 * @param {string} encoding Ignored
 * @param {function(Error)} callback Callback to call when finished writing
 */
function _write(chunk, encoding, callback) {
  this._stream.write(chunk, encoding, callback);
}

/**
 * See docs for _write
 */
ServerWritableObjectStream.prototype._write = _write;

/**
 * Creates a binary stream handler function from a unary handler function
 * @param {function(Object, function(Error, *))} handler Unary call handler
 * @return {function(stream)} Binary stream handler
 */
function makeUnaryHandler(handler) {
  /**
   * Handles a stream by reading a single data value, passing it to the handler,
   * and writing the response back to the stream.
   * @param {stream} stream Binary data stream
   */
  return function handleUnaryCall(stream) {
    stream.on('data', function handleUnaryData(value) {
      var call = {request: value};
      Object.defineProperty(call, 'cancelled', {
        get: function() { return stream.cancelled;}
      });
      handler(call, function sendUnaryData(err, value) {
        if (err) {
          stream.emit('error', err);
        } else {
          stream.write(value);
          stream.end();
        }
      });
    });
  };
}

/**
 * Creates a binary stream handler function from a client stream handler
 * function
 * @param {function(Readable, function(Error, *))} handler Client stream call
 *     handler
 * @return {function(stream)} Binary stream handler
 */
function makeClientStreamHandler(handler) {
  /**
   * Handles a stream by passing a deserializing stream to the handler and
   * writing the response back to the stream.
   * @param {stream} stream Binary data stream
   */
  return function handleClientStreamCall(stream) {
    var object_stream = new ServerReadableObjectStream(stream);
    handler(object_stream, function sendClientStreamData(err, value) {
        if (err) {
          stream.emit('error', err);
        } else {
          stream.write(value);
          stream.end();
        }
    });
  };
}

/**
 * Creates a binary stream handler function from a server stream handler
 * function
 * @param {function(Writable)} handler Server stream call handler
 * @return {function(stream)} Binary stream handler
 */
function makeServerStreamHandler(handler) {
  /**
   * Handles a stream by attaching it to a serializing stream, and passing it to
   * the handler.
   * @param {stream} stream Binary data stream
   */
  return function handleServerStreamCall(stream) {
    stream.on('data', function handleClientData(value) {
      var object_stream = new ServerWritableObjectStream(stream);
      object_stream.request = value;
      handler(object_stream);
    });
  };
}

/**
 * Creates a binary stream handler function from a bidi stream handler function
 * @param {function(Duplex)} handler Unary call handler
 * @return {function(stream)} Binary stream handler
 */
function makeBidiStreamHandler(handler) {
  return handler;
}

/**
 * Map with short names for each of the handler maker functions. Used in
 * makeServerConstructor
 */
var handler_makers = {
  unary: makeUnaryHandler,
  server_stream: makeServerStreamHandler,
  client_stream: makeClientStreamHandler,
  bidi: makeBidiStreamHandler
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
   * @param {Object} options Options to pass to the underlying server
   */
  function SurfaceServer(service_handlers, options) {
    var server = new Server(options);
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
        var binary_handler = handler_makers[method_type](
            service_handlers[service_name][decapitalize(method.name)]);
        var serialize = common.serializeCls(
            method.resolvedResponseType.build());
        var deserialize = common.deserializeCls(
            method.resolvedRequestType.build());
        server.register(prefix + capitalize(method.name), binary_handler,
                        serialize, deserialize);
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
    this.inner_server.start();
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
