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
 * @param {function(*):Buffer=} serialize Serialization function for requests
 * @param {function(Buffer):*=} deserialize Deserialization function for
 *     responses
 */
function GrpcClientStream(call, serialize, deserialize) {
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
  var self = this;
  var finished = false;
  // Indicates that a read is currently pending
  var reading = false;
  // Indicates that a write is currently pending
  var writing = false;
  this._call = call;

  /**
   * Serialize a request value to a buffer. Always maps null to null. Otherwise
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
   * Deserialize a response buffer to a value. Always maps null to null.
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
    if (self.push(self.deserialize(data)) && data != null) {
      self._call.startRead(readCallback);
    } else {
      reading = false;
    }
  }
  call.invoke(function(event) {
    self.emit('metadata', event.data);
  }, function(event) {
    finished = true;
    self.emit('status', event.data);
  }, 0);
  this.on('finish', function() {
    call.writesDone(function() {});
  });
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
 * Start reading. This is an implementation of a method needed for implementing
 * stream.Readable.
 * @param {number} size Ignored
 */
GrpcClientStream.prototype._read = function(size) {
  this.startReading();
};

/**
 * Attempt to write the given chunk. Calls the callback when done. This is an
 * implementation of a method needed for implementing stream.Writable.
 * @param {Buffer} chunk The chunk to write
 * @param {string} encoding Ignored
 * @param {function(Error=)} callback Ignored
 */
GrpcClientStream.prototype._write = function(chunk, encoding, callback) {
  var self = this;
  self._call.startWrite(self.serialize(chunk), function(event) {
    callback();
  }, 0);
};

/**
 * Cancel the ongoing call. If the call has not already finished, it will finish
 * with status CANCELLED.
 */
GrpcClientStream.prototype.cancel = function() {
  self._call.cancel();
};

/**
 * Make a request on the channel to the given method with the given arguments
 * @param {grpc.Channel} channel The channel on which to make the request
 * @param {string} method The method to request
 * @param {function(*):Buffer} serialize Serialization function for requests
 * @param {function(Buffer):*} deserialize Deserialization function for
 *     responses
 * @param {array=} metadata Array of metadata key/value pairs to add to the call
 * @param {(number|Date)=} deadline The deadline for processing this request.
 *     Defaults to infinite future.
 * @return {stream=} The stream of responses
 */
function makeRequest(channel,
                     method,
                     serialize,
                     deserialize,
                     metadata,
                     deadline) {
  if (deadline === undefined) {
    deadline = Infinity;
  }
  var call = new grpc.Call(channel, method, deadline);
  if (metadata) {
    call.addMetadata(metadata);
  }
  return new GrpcClientStream(call, serialize, deserialize);
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
