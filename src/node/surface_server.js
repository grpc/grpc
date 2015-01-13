var _ = require('underscore');

var Server = require('./server.js');

var stream = require('stream');

var Readable = stream.Readable;
var Writable = stream.Writable;
var Duplex = stream.Duplex;
var util = require('util');

util.inherits(ServerReadableObjectStream, Readable);

/**
 * Class for representing a gRPC client streaming call as a Node stream on the
 * server side. Extends from stream.Readable.
 * @constructor
 * @param {stream} stream Underlying binary Duplex stream for the call
 * @param {function(Buffer)} deserialize Function for deserializing binary data
 * @param {object} options Stream options
 */
function ServerReadableObjectStream(stream, deserialize, options) {
  options = _.extend(options, {objectMode: true});
  Readable.call(this, options);
  this._stream = stream;
  Object.defineProperty(this, 'cancelled', {
    get: function() { return stream.cancelled; }
  });
  var self = this;
  this._stream.on('data', function forwardData(chunk) {
    if (!self.push(deserialize(chunk))) {
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
 * @param {function(*):Buffer} serialize Function for serializing objects
 * @param {object} options Stream options
 */
function ServerWritableObjectStream(stream, serialize, options) {
  options = _.extend(options, {objectMode: true});
  Writable.call(this, options);
  this._stream = stream;
  this._serialize = serialize;
  this.on('finish', function() {
    this._stream.end();
  });
}

util.inherits(ServerBidiObjectStream, Duplex);

/**
 * Class for representing a gRPC bidi streaming call as a Node stream on the
 * server side. Extends from stream.Duplex.
 * @constructor
 * @param {stream} stream Underlying binary Duplex stream for the call
 * @param {function(*):Buffer} serialize Function for serializing objects
 * @param {function(Buffer)} deserialize Function for deserializing binary data
 * @param {object} options Stream options
 */
function ServerBidiObjectStream(stream, serialize, deserialize, options) {
  options = _.extend(options, {objectMode: true});
  Duplex.call(this, options);
  this._stream = stream;
  this._serialize = serialize;
  var self = this;
  this._stream.on('data', function forwardData(chunk) {
    if (!self.push(deserialize(chunk))) {
      self._stream.pause();
    }
  });
  this._stream.on('end', function forwardEnd() {
    self.push(null);
  });
  this._stream.pause();
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
 * See docs for _read
 */
ServerBidiObjectStream.prototype._read = _read;

/**
 * _write implementation for both types of streams that allow writing
 * @this {ServerWritableObjectStream|ServerBidiObjectStream}
 * @param {*} chunk The value to write to the stream
 * @param {string} encoding Ignored
 * @param {function(Error)} callback Callback to call when finished writing
 */
function _write(chunk, encoding, callback) {
  this._stream.write(this._serialize(chunk), encoding, callback);
}

/**
 * See docs for _write
 */
ServerWritableObjectStream.prototype._write = _write;
/**
 * See docs for _write
 */
ServerBidiObjectStream.prototype._write = _write;

/**
 * Creates a binary stream handler function from a unary handler function
 * @param {function(Object, function(Error, *))} handler Unary call handler
 * @param {function(*):Buffer} serialize Serialization function
 * @param {function(Buffer):*} deserialize Deserialization function
 * @return {function(stream)} Binary stream handler
 */
function makeUnaryHandler(handler, serialize, deserialize) {
  /**
   * Handles a stream by reading a single data value, passing it to the handler,
   * and writing the response back to the stream.
   * @param {stream} stream Binary data stream
   */
  return function handleUnaryCall(stream) {
    stream.on('data', function handleUnaryData(value) {
      var call = {request: deserialize(value)};
      Object.defineProperty(call, 'cancelled', {
        get: function() { return stream.cancelled;}
      });
      handler(call, function sendUnaryData(err, value) {
        if (err) {
          stream.emit('error', err);
        } else {
          stream.write(serialize(value));
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
 * @param {function(*):Buffer} serialize Serialization function
 * @param {function(Buffer):*} deserialize Deserialization function
 * @return {function(stream)} Binary stream handler
 */
function makeClientStreamHandler(handler, serialize, deserialize) {
  /**
   * Handles a stream by passing a deserializing stream to the handler and
   * writing the response back to the stream.
   * @param {stream} stream Binary data stream
   */
  return function handleClientStreamCall(stream) {
    var object_stream = new ServerReadableObjectStream(stream, deserialize, {});
    handler(object_stream, function sendClientStreamData(err, value) {
        if (err) {
          stream.emit('error', err);
        } else {
          stream.write(serialize(value));
          stream.end();
        }
    });
  };
}

/**
 * Creates a binary stream handler function from a server stream handler
 * function
 * @param {function(Writable)} handler Server stream call handler
 * @param {function(*):Buffer} serialize Serialization function
 * @param {function(Buffer):*} deserialize Deserialization function
 * @return {function(stream)} Binary stream handler
 */
function makeServerStreamHandler(handler, serialize, deserialize) {
  /**
   * Handles a stream by attaching it to a serializing stream, and passing it to
   * the handler.
   * @param {stream} stream Binary data stream
   */
  return function handleServerStreamCall(stream) {
    stream.on('data', function handleClientData(value) {
      var object_stream = new ServerWritableObjectStream(stream,
                                                         serialize,
                                                         {});
      object_stream.request = deserialize(value);
      handler(object_stream);
    });
  };
}

/**
 * Creates a binary stream handler function from a bidi stream handler function
 * @param {function(Duplex)} handler Unary call handler
 * @param {function(*):Buffer} serialize Serialization function
 * @param {function(Buffer):*} deserialize Deserialization function
 * @return {function(stream)} Binary stream handler
 */
function makeBidiStreamHandler(handler, serialize, deserialize) {
  /**
   * Handles a stream by wrapping it in a serializing and deserializing object
   * stream, and passing it to the handler.
   * @param {stream} stream Binary data stream
   */
  return function handleBidiStreamCall(stream) {
    var object_stream = new ServerBidiObjectStream(stream,
                                                   serialize,
                                                   deserialize,
                                                   {});
    handler(object_stream);
  };
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
function makeServerConstructor(methods, prefix) {
  /**
   * Create a server with the given handlers for all of the methods.
   * @constructor
   * @param {Object} handlers Map from method names to method handlers.
   * @param {Object} options Options to pass to the underlying server
   */
  function SurfaceServer(handlers, options) {
    var server = new Server(options);
    this.inner_server = server;
    _.each(handlers, function(handler, name) {
      var method = methods[name];
      var method_type;
      if (method.client_stream) {
        if (method.server_stream) {
          method_type = 'bidi';
        } else {
          method_type = 'client_stream';
        }
      } else {
        if (method.server_stream) {
          method_type = 'server_stream';
        } else {
          method_type = 'unary';
        }
      }
      var binary_handler = handler_makers[method_type](handler,
                                                       method.serialize,
                                                       method.deserialize);
      server.register('' + prefix + name, binary_handler);
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
    this.inner_server.bind(port, secure);
    return this;
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
