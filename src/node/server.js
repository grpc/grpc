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
 * @param {object} options Stream options
 */
function GrpcServerStream(call, options) {
  Duplex.call(this, options);
  this._call = call;
  // Indicate that a status has been sent
  var finished = false;
  var self = this;
  var status = {
    'code' : grpc.status.OK,
    'details' : 'OK'
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
    if (self.push(data) && data != null) {
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
  self._call.startWrite(chunk, function(event) {
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
      var stream = new GrpcServerStream(call);
      Object.defineProperty(stream, 'cancelled', {
        get: function() { return cancelled;}
      });
      try {
        handler(stream, data.metadata);
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
 * @return {boolean} True if the handler was set. False if a handler was already
 *     set for that name.
 */
Server.prototype.register = function(name, handler) {
  if (this.handlers.hasOwnProperty(name)) {
    return false;
  }
  this.handlers[name] = handler;
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
    this._server.addSecureHttp2Port(port);
  } else {
    this._server.addHttp2Port(port);
  }
};

/**
 * See documentation for Server
 */
module.exports = Server;
