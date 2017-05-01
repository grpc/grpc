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
/**
 * Client Batches module
 *
 * Defines the batch types a client can process and provides methods to wire
 * inbound and outbound batch logic into a call.
 * @module
 */
'use strict';

var _ = require('lodash');
var constants = require('./constants');
var grpc = require('./grpc_extension');
var Metadata = require('./metadata');

var BATCH_TYPE = {
  UNARY: 0,
  METADATA: 1,
  CLOSE: 2,
  SEND_STREAMING: 3,
  RECV_STREAMING: 4,
  STATUS: 5,
  SEND_SYNC: 6,
  RECV_SYNC: 7
};

var ACTIONS = {};
ACTIONS[BATCH_TYPE.UNARY] = _registerUnaryBatch;
ACTIONS[BATCH_TYPE.METADATA] = _registerMetadataBatch;
ACTIONS[BATCH_TYPE.RECV_SYNC] = _registerRecvSync;
ACTIONS[BATCH_TYPE.SEND_STREAMING] = _registerSendStreaming;
ACTIONS[BATCH_TYPE.CLOSE] = _registerClose;
ACTIONS[BATCH_TYPE.SEND_SYNC] = _registerSendSync;
ACTIONS[BATCH_TYPE.STATUS] = _registerStatus;
ACTIONS[BATCH_TYPE.RECV_STREAMING] = _registerRecvStreaming;

/**
 * A container for the properties of a batch definition
 * @param {number[]} batch_ops The operations performed by the batch
 * @param {number[]} trigger_ops The operations which can trigger the
 * execution of a batch. Useful for determining when to start batches which
 * only include receive operations.
 * @param {function} outbound_handler Runs when all the outbound operations are
 * ready.
 * @param {function} inbound_handler Runs when all the inbound operations are
 * @constructor
 */
function BatchDefinition(batch_ops, trigger_ops, outbound_handler,
                         inbound_handler) {
  this.batch_ops = batch_ops;
  this.trigger_ops = trigger_ops;
  this.outbound_handler = outbound_handler;
  this.inbound_handler = inbound_handler;
}

/**
 * Returns the appropriate batch handler (inbound or outbound)
 * @param {boolean} is_outbound
 * @returns {function}
 */
BatchDefinition.prototype.getHandler = function(is_outbound) {
  return is_outbound ? this.outbound_handler : this.inbound_handler;
};

/**
 * A list of batch definitions, used to group the batches for each
 * RPC type and maintain the state of each operation.
 * @constructor
 */
function BatchRegistry() {
  this.batches = [];
}

/**
 * Create a batch definition and add it to the registry
 * @param {number[]} batch_ops The operations performed by the batch
 * @param {number[]} trigger_ops The operations which can trigger the
 * execution of a batch. Useful for determining when to start batches which
 * only include receive operations.
 * @param {function} outbound_handler Runs when all the outbound operations are
 * ready.
 * @param {function} inbound_handler Runs when all the inbound operations are
 * ready.
 */
BatchRegistry.prototype.add = function(batch_ops, trigger_ops, outbound_handler,
                                       inbound_handler) {
  var definition = new BatchDefinition(batch_ops, trigger_ops, outbound_handler,
    inbound_handler);
  this.batches.push(definition);
};

/**
 * Populate a BatchRegistry with a set of batch handlers linked to a given
 * EventEmitter and the consumer's callback.
 * @param {EventEmitter} emitter Sends events to the consumer on batch
 * completion.
 * @param {BatchRegistry} batch_registry A container for the batch handlers
 * @param {number[]} batch_types A list of batch types to handle
 * @param {object} options
 * @param {function} callback The consumer's callback, executed when batches
 * complete
 */
function registerBatches(emitter, batch_registry, batch_types, options,
                          callback) {
  _.each(batch_types, function(batch_type) {
    ACTIONS[batch_type](emitter, batch_registry, options, callback);
  });
}

/**
 * Create handlers for the unary batch logic and add them to a registry
 * @param {EventEmitter} emitter Sends events to the consumer on batch
 * completion.
 * @param {BatchRegistry} batch_registry A container for the batch handlers
 * @param {object} options
 * @param {function} callback The consumer's callback, executed when batches
 * complete
 * @private
 */
function _registerUnaryBatch(emitter, batch_registry, options, callback) {
  var handle_inbound = function(batch) {
    var metadata = batch[grpc.opType.RECV_INITIAL_METADATA];
    var message = batch[grpc.opType.RECV_MESSAGE];
    var status = batch[grpc.opType.RECV_STATUS_ON_CLIENT];
    var error;
    emitter.emit('metadata', metadata);
    if (status.code !== constants.status.OK) {
      error = new Error(status.details);
      error.code = status.code;
      error.metadata = status.metadata;
      callback(error);
    } else {
      callback(null, message);
    }
    emitter.emit('status', status);
  };

  var handle_outbound = function(batch, call, listener) {
    var raw_message = batch[grpc.opType.SEND_MESSAGE];
    var message = options.method_descriptor.serialize(raw_message);
    if (options) {
      message.grpcWriteFlags = options.flags;
    }
    var raw_metadata = batch[grpc.opType.SEND_INITIAL_METADATA];
    var metadata = raw_metadata._getCoreRepresentation();
    batch[grpc.opType.SEND_MESSAGE] = message;
    batch[grpc.opType.SEND_INITIAL_METADATA] = metadata;
    batch[grpc.opType.SEND_CLOSE_FROM_CLIENT] = true;
    batch[grpc.opType.RECV_INITIAL_METADATA] = true;
    batch[grpc.opType.RECV_MESSAGE] = true;
    batch[grpc.opType.RECV_STATUS_ON_CLIENT] = true;

    call.startBatch(batch, function(err, response) {
      response.status.metadata = Metadata._fromCoreRepresentation(
        response.status.metadata);
      var status = response.status;
      var deserialized;
      if (status.code === constants.status.OK) {
        if (err) {
          // Got a batch error, but OK status. Something went wrong
          callback(err);
          return;
        } else {
          try {
            deserialized = options.method_descriptor.deserialize(response.read);
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
      response.metadata = Metadata._fromCoreRepresentation(response.metadata);
      listener.onReceiveMetadata(response.metadata);
      listener.onReceiveMessage(deserialized);
      listener.onReceiveStatus(status);
    });
  };

  var batch_ops = [
    grpc.opType.SEND_INITIAL_METADATA,
    grpc.opType.SEND_MESSAGE,
    grpc.opType.SEND_CLOSE_FROM_CLIENT,
    grpc.opType.RECV_INITIAL_METADATA,
    grpc.opType.RECV_MESSAGE,
    grpc.opType.RECV_STATUS_ON_CLIENT
  ];

  batch_registry.add(batch_ops, batch_ops, handle_outbound, handle_inbound);
}

/**
 * Create handlers for the metadata batch logic and add them to a registry
 * @param {EventEmitter} emitter Sends events to the consumer on batch
 * completion.
 * @param {BatchRegistry} batch_registry A container for the batch handlers
 * @private
 */
function _registerMetadataBatch(emitter, batch_registry) {

  var handle_inbound = function(batch) {
    var metadata = batch[grpc.opType.RECV_INITIAL_METADATA];
    emitter.emit('metadata', metadata);
  };

  var handle_outbound = function(batch, call, listener) {
    var metadata_batch = {};
    var raw_metadata = batch[grpc.opType.SEND_INITIAL_METADATA];
    var metadata = raw_metadata._getCoreRepresentation();
    metadata_batch[grpc.opType.SEND_INITIAL_METADATA] = metadata;
    metadata_batch[grpc.opType.RECV_INITIAL_METADATA] = true;
    call.startBatch(metadata_batch, function(err, response) {
      if (err) {
        // The call has stopped for some reason. A non-OK status will arrive
        // in the other batch.
        return;
      }
      response.metadata = Metadata._fromCoreRepresentation(response.metadata);
      listener.onReceiveMetadata(response.metadata);
    });
  };

  var batch_ops = [
    grpc.opType.SEND_INITIAL_METADATA,
    grpc.opType.RECV_INITIAL_METADATA
  ];

  batch_registry.add(batch_ops, batch_ops, handle_outbound, handle_inbound);
}

/**
 * Create handlers for the synchronous message receive batch logic and
 * add them to a registry.
 * @param {EventEmitter} emitter Sends events to the consumer on batch
 * completion.
 * @param {BatchRegistry} batch_registry A container for the batch handlers
 * @param {object} options
 * @param {function} callback The consumer's callback, executed when batches
 * complete
 * @private
 */
function _registerRecvSync(emitter, batch_registry, options, callback) {
  var handle_inbound = function(batch) {
    var message = batch[grpc.opType.RECV_MESSAGE];
    var status = batch[grpc.opType.RECV_STATUS_ON_CLIENT];
    var error;
    if (status.code !== constants.status.OK) {
      error = new Error(status.details);
      error.code = status.code;
      error.metadata = status.metadata;
      callback(error);
    } else {
      callback(null, message);
    }
    emitter.emit('status', status);
  };

  var handle_outbound = function(batch, call, listener) {
    var client_batch = {};
    client_batch[grpc.opType.RECV_MESSAGE] = true;
    client_batch[grpc.opType.RECV_STATUS_ON_CLIENT] = true;
    call.startBatch(client_batch, function(err, response) {
      response.status.metadata = Metadata._fromCoreRepresentation(
        response.status.metadata);
      var status = response.status;
      var deserialized;
      if (status.code === constants.status.OK) {
        if (err) {
          // Got a batch error, but OK status. Something went wrong
          callback(err);
          return;
        } else {
          try {
            deserialized = options.method_descriptor.deserialize(response.read);
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
      listener.onReceiveMessage(deserialized);
      listener.onReceiveStatus(status);
    });
  };

  var batch_ops = [
    grpc.opType.RECV_MESSAGE,
    grpc.opType.RECV_STATUS_ON_CLIENT
  ];

  var trigger_ops = [
    grpc.opType.SEND_INITIAL_METADATA,
    grpc.opType.RECV_MESSAGE,
    grpc.opType.RECV_STATUS_ON_CLIENT
  ];

  batch_registry.add(batch_ops, trigger_ops, handle_outbound, handle_inbound);
}

/**
 * Create handlers for the stream message send batch logic and add them to a
 * registry.
 * @param {EventEmitter} emitter Sends events to the consumer on batch
 * completion.
 * @param {BatchRegistry} batch_registry A container for the batch handlers
 * @param {object} options
 * @private
 */
function _registerSendStreaming(emitter, batch_registry, options) {

  var handle_outbound = function(batch, call, listener, context) {
    var message;
    var chunk = batch[grpc.opType.SEND_MESSAGE];
    var callback = context.callback;
    var encoding = context.encoding;
    if (emitter.writeFailed) {
      /* Once a write fails, just call the callback immediately to let the
         caller flush any pending writes. */
      callback();
    }
    try {
      message = options.method_descriptor.serialize(chunk);
    } catch (e) {
      /* Sending this error to the server and emitting it immediately on the
       client may put the call in a slightly weird state on the client side,
       but passing an object that causes a serialization failure is a misuse
       of the API anyway, so that's OK. The primary purpose here is to give the
       programmer a useful error and to stop the stream properly */
      call.cancelWithStatus(constants.status.INTERNAL, 'Serialization failure');
      if (callback) {
        callback(e);
      }
    }
    if (_.isFinite(encoding)) {
      /* Attach the encoding if it is a finite number. This is the closest we
       * can get to checking that it is valid flags */
      message.grpcWriteFlags = encoding;
    } else {
      message.grpcWriteFlags = options.flags;
    }
    var streaming_batch = {};
    streaming_batch[grpc.opType.SEND_MESSAGE] = message;
    call.startBatch(streaming_batch, function(err, event) {
      if (err) {
        /* Assume that the call is complete and that writing failed because a
           status was received. In that case, set a flag to discard all future
           writes */
        emitter.writeFailed = true;
        return;
      }
      if (callback) {
        callback();
      }
    });
  };
  var batch_ops = [
    grpc.opType.SEND_MESSAGE
  ];

  batch_registry.add(batch_ops, batch_ops, handle_outbound);
}

/**
 * Create handlers for the client-close batch logic and add them to a registry.
 * @param {EventEmitter} emitter Sends events to the consumer on batch
 * completion.
 * @param {BatchRegistry} batch_registry A container for the batch handlers
 * @private
 */
function _registerClose(emitter, batch_registry) {
  var handle_outbound = function(batch, call) {
    var end_batch = {};
    end_batch[grpc.opType.SEND_CLOSE_FROM_CLIENT] = true;
    call.startBatch(end_batch, function() {});
  };

  var batch_ops = [
    grpc.opType.SEND_CLOSE_FROM_CLIENT
  ];

  batch_registry.add(batch_ops, batch_ops, handle_outbound);
}

/**
 * Create handlers for the synchronous message send logic and add them to a
 * registry.
 * @param {EventEmitter} emitter Sends events to the consumer on batch
 * completion.
 * @param {BatchRegistry} batch_registry A container for the batch handlers
 * @param {object} options
 * @private
 */
function _registerSendSync(emitter, batch_registry, options) {

  var handle_inbound = function(batch) {
    var metadata = batch[grpc.opType.RECV_INITIAL_METADATA];
    emitter.emit('metadata', metadata);
  };

  var handle_outbound = function(batch, call, listener) {
    var start_batch = {};
    var raw_message = batch[grpc.opType.SEND_MESSAGE];
    var message = options.method_descriptor.serialize(raw_message);
    if (options) {
      message.grpcWriteFlags = options.flags;
    }
    var raw_metadata = batch[grpc.opType.SEND_INITIAL_METADATA];
    var metadata = raw_metadata._getCoreRepresentation();
    start_batch[grpc.opType.SEND_INITIAL_METADATA] = metadata;
    start_batch[grpc.opType.SEND_MESSAGE] = message;
    start_batch[grpc.opType.SEND_CLOSE_FROM_CLIENT] = true;
    start_batch[grpc.opType.RECV_INITIAL_METADATA] = true;
    call.startBatch(start_batch, function(err, response) {
      if (err) {
        // The call has stopped for some reason. A non-OK status will arrive
        // in the other batch.
        return;
      }
      response.metadata = Metadata._fromCoreRepresentation(response.metadata);
      listener.onReceiveMetadata(response.metadata);
    });
  };

  var batch_ops = [
    grpc.opType.SEND_INITIAL_METADATA,
    grpc.opType.RECV_INITIAL_METADATA,
    grpc.opType.SEND_MESSAGE,
    grpc.opType.SEND_CLOSE_FROM_CLIENT
  ];

  batch_registry.add(batch_ops, batch_ops, handle_outbound, handle_inbound);
}

/**
 * Create handlers for the status receive batch logic and add them to a
 * registry.
 * @param {EventEmitter} emitter Sends events to the consumer on batch
 * completion.
 * @param {BatchRegistry} batch_registry A container for the batch handlers
 * @private
 */
function _registerStatus(emitter, batch_registry) {
  var handle_inbound = function(batch) {
    var status = batch[grpc.opType.RECV_STATUS_ON_CLIENT];
    emitter._receiveStatus(status);
  };

  var handle_outbound = function(batch, call, listener) {
    var status_batch = {};
    status_batch[grpc.opType.RECV_STATUS_ON_CLIENT] = true;
    call.startBatch(status_batch, function(err, response) {
      if (err) {
        emitter.emit('error', err);
        return;
      }
      response.status.metadata = Metadata._fromCoreRepresentation(
        response.status.metadata);
      listener.onReceiveStatus(response.status);
    });
  };

  var batch_ops = [
    grpc.opType.RECV_STATUS_ON_CLIENT
  ];

  var trigger_ops = [
    grpc.opType.SEND_INITIAL_METADATA,
    grpc.opType.RECV_STATUS_ON_CLIENT
  ];

  batch_registry.add(batch_ops, trigger_ops, handle_outbound, handle_inbound);
}

/**
 * Create handlers for the streaming message receive batch logic and add them
 * to a registry.
 * @param {EventEmitter} emitter Sends events to the consumer on batch
 * completion.
 * @param {BatchRegistry} batch_registry A container for the batch handlers
 * @param {object} options
 * @private
 */
function _registerRecvStreaming(emitter, batch_registry, options) {
  var getCallback = function(stream, call, listener) {
    return function(err, response) {
      if (err) {
        // Something has gone wrong. Stop reading and wait for status
        stream.finished = true;
        stream._readsDone();
        return;
      }
      var context = {
        stream: stream,
        call: call,
        listener: listener
      };
      var data = response.read;
      var deserialized;
      try {
        deserialized = options.method_descriptor.deserialize(data);
      } catch (e) {
        stream._readsDone({code: constants.status.INTERNAL,
          details: 'Failed to parse server response'});
        return;
      }
      if (data === null) {
        stream._readsDone();
        return;
      }
      listener.recvMessageWithContext(context, deserialized);
    };
  };
  var handle_inbound = function(batch, context) {
    var message = batch[grpc.opType.RECV_MESSAGE];
    var stream_obj = context.stream;
    var call = context.call;
    var listener = context.listener;
    if (stream_obj.push(message) && message !== null) {
      var read_batch = {};
      read_batch[grpc.opType.RECV_MESSAGE] = true;
      call.startBatch(read_batch, getCallback(context.stream, call, listener));
    } else {
      stream_obj.reading = false;
    }
  };

  var handle_outbound = function(batch, call, listener, context) {
    context.call = call;
    context.listener = listener;
    var read_batch = {};
    read_batch[grpc.opType.RECV_MESSAGE] = true;
    call.startBatch(read_batch, getCallback(context.stream, call, listener));
  };

  var batch_ops = [
    grpc.opType.RECV_MESSAGE
  ];

  var trigger_ops = [
    grpc.opType.RECV_MESSAGE
  ];

  batch_registry.add(batch_ops, trigger_ops, handle_outbound, handle_inbound);
}

module.exports.registerBatches = registerBatches;
module.exports.BATCH_TYPE = BATCH_TYPE;
module.exports.BatchRegistry = BatchRegistry;
