var grpc = require('bindings')('grpc.node');

var common = require('./common');

var Duplex = require('stream').Duplex;
var util = require('util');

util.inherits(GrpcClientStream, Duplex);

/**
 * Class for representing a gRPC client side stream as a Node stream. Extends
 * from stream.Duplex.
 * @constructor
 * @param {grpc.Call} call Call object to proxy
 * @param {object} options Stream options
 */
function GrpcClientStream(call, options) {
  Duplex.call(this, options);
  var self = this;
  // Indicates that we can start reading and have not received a null read
  var can_read = false;
  // Indicates that a read is currently pending
  var reading = false;
  // Indicates that we can call startWrite
  var can_write = false;
  // Indicates that a write is currently pending
  var writing = false;
  this._call = call;
  /**
   * Callback to handle receiving a READ event. Pushes the data from that event
   * onto the read queue and starts reading again if applicable.
   * @param {grpc.Event} event The READ event object
   */
  function readCallback(event) {
    var data = event.data;
    if (self.push(data)) {
      if (data == null) {
        // Disable starting to read after null read was received
        can_read = false;
        reading = false;
      } else {
        call.startRead(readCallback);
      }
    } else {
      // Indicate that reading can be resumed by calling startReading
      reading = false;
    }
  };
  /**
   * Initiate a read, which continues until self.push returns false (indicating
   * that reading should be paused) or data is null (indicating that there is no
   * more data to read).
   */
  function startReading() {
    call.startRead(readCallback);
  }
  // TODO(mlumish): possibly change queue implementation due to shift slowness
  var write_queue = [];
  /**
   * Write the next chunk of data in the write queue if there is one. Otherwise
   * indicate that there is no pending write. When the write succeeds, this
   * function is called again.
   */
  function writeNext() {
    if (write_queue.length > 0) {
      writing = true;
      var next = write_queue.shift();
      var writeCallback = function(event) {
        next.callback();
        writeNext();
      };
      call.startWrite(next.chunk, writeCallback, 0);
    } else {
      writing = false;
    }
  }
  call.startInvoke(function(event) {
    can_read = true;
    can_write = true;
    startReading();
    writeNext();
  }, function(event) {
    self.emit('metadata', event.data);
  }, function(event) {
    self.emit('status', event.data);
  }, 0);
  this.on('finish', function() {
    call.writesDone(function() {});
  });
  /**
   * Indicate that reads should start, and start them if the INVOKE_ACCEPTED
   * event has been received.
   */
  this._enableRead = function() {
    if (!reading) {
      reading = true;
      if (can_read) {
        startReading();
      }
    }
  };
  /**
   * Push the chunk onto the write queue, and write from the write queue if
   * there is not a pending write
   * @param {Buffer} chunk The chunk of data to write
   * @param {function(Error=)} callback The callback to call when the write
   *     completes
   */
  this._tryWrite = function(chunk, callback) {
    write_queue.push({chunk: chunk, callback: callback});
    if (can_write && !writing) {
      writeNext();
    }
  };
}

/**
 * Start reading. This is an implementation of a method needed for implementing
 * stream.Readable.
 * @param {number} size Ignored
 */
GrpcClientStream.prototype._read = function(size) {
  this._enableRead();
};

/**
 * Attempt to write the given chunk. Calls the callback when done. This is an
 * implementation of a method needed for implementing stream.Writable.
 * @param {Buffer} chunk The chunk to write
 * @param {string} encoding Ignored
 * @param {function(Error=)} callback Ignored
 */
GrpcClientStream.prototype._write = function(chunk, encoding, callback) {
  this._tryWrite(chunk, callback);
};

/**
 * Make a request on the channel to the given method with the given arguments
 * @param {grpc.Channel} channel The channel on which to make the request
 * @param {string} method The method to request
 * @param {array=} metadata Array of metadata key/value pairs to add to the call
 * @param {(number|Date)=} deadline The deadline for processing this request.
 *     Defaults to infinite future.
 * @return {stream=} The stream of responses
 */
function makeRequest(channel,
                     method,
                     metadata,
                     deadline) {
  if (deadline === undefined) {
    deadline = Infinity;
  }
  var call = new grpc.Call(channel, method, deadline);
  if (metadata) {
    call.addMetadata(metadata);
  }
  return new GrpcClientStream(call);
}

/**
 * See documentation for makeRequest above
 */
exports.makeRequest = makeRequest;

/**
 * Represents a client side gRPC channel associated with a single host.
 */
exports.Channel = grpc.Channel;
/**
 * Status name to code number mapping
 */
exports.status = grpc.status;
/**
 * Call error name to code number mapping
 */
exports.callError = grpc.callError;
