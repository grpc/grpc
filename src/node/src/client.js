/**
 * @license
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
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

var batches = require('./client_batches');
var client_interceptors = require('./client_interceptors');
var grpc = require('./grpc_extension');

var common = require('./common');

var Metadata = require('./metadata');

var constants = require('./constants');

var EventEmitter = require('events').EventEmitter;

var stream = require('stream');

var BatchRegistry = batches.BatchRegistry;
var BATCH_TYPE = batches.BATCH_TYPE;
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
 */
function ClientWritableStream(call) {
  Writable.call(this, {objectMode: true});
  this.call = call;
  this.on('finish', function() {
    call.halfClose();
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
  var context = {
    encoding: encoding,
    callback: callback
  };
  this.call.sendMessageWithContext(context, chunk);
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
 */
function ClientReadableStream(call) {
  Readable.call(this, {objectMode: true});
  this.call = call;
  this.finished = false;
  this.reading = false;
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
  if (self.finished) {
    self.push(null);
  } else {
    if (!self.reading) {
      self.reading = true;
      var context = {
        stream: self
      };
      self.call.recvMessageWithContext(context);
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
 */
function ClientDuplexStream(call) {
  Duplex.call(this, {objectMode: true});
  this.call = call;
  /* Status generated from reading messages from the server. Overrides the
   * status from the server if not OK */
  this.read_status = null;
  /* Status received from the server. */
  this.received_status = null;
  this.on('finish', function() {
    call.halfClose();
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

var MethodType = {
  UNARY: 0,
  CLIENT_STREAMING: 1,
  SERVER_STREAMING: 2,
  BIDI_STREAMING: 3
};

exports.MethodType = MethodType;

/**
 * A container for the properties of a gRPC method.
 * @param {string} name
 * @param {string} service_name
 * @param {string} path
 * @param {MethodType} method_type
 * @param {Function} serialize
 * @param {Function} deserialize
 * @constructor
 */
function MethodDescriptor(name, service_name, path, method_type, serialize,
                          deserialize) {
  this.name = name;
  this.service_name = service_name;
  this.path = path;
  this.method_type = method_type;
  this.serialize = serialize;
  this.deserialize = deserialize;
}

exports.MethodDescriptor = MethodDescriptor;

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
 * @param {grpc.Channel} channel
 * @param {MethodDescriptor} method_descriptor
 * @param {grpc.Client~CallOptions=} options Options object.
 */
function getCall(channel, method_descriptor, options) {
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
  var call = new grpc.Call(channel, method_descriptor.path, deadline, host,
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
  var self = this;
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

  // Resolve interceptor options and assign interceptors to each method
  var interceptor_providers = options.interceptor_providers || [];
  delete options.interceptor_providers;
  var interceptors = options.interceptors || [];
  delete options.interceptors;
  if (interceptor_providers.length && interceptors.length) {
    throw new client_interceptors.InterceptorConfigurationError(
      'Both interceptors and interceptor_providers were passed as options ' +
      'to the client constructor. Only one of these is allowed.');
  }
  _.each(self.$method_descriptors, function(method_descriptor) {
    self[method_descriptor.name].interceptors = client_interceptors
      .resolveInterceptorProviders(interceptor_providers, method_descriptor)
      .concat(interceptors);
  });
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
 * Make a unary request to the given method, using the given method
 * descriptor, with the given argument.
 * @param {MethodDescriptor} method_descriptor A container of method attributes
 * @param {*} argument The argument to the call. Should be serializable with
 *     method_descriptor.serialize
 * @param {grpc.Metadata=} metadata Metadata to add to the call
 * @param {grpc.Client~CallOptions=} options Options map
 * @param {grpc.Client~requestCallback} callback The callback to call
 *     when the response is received
 * @return {grpc~ClientUnaryCall} An event emitter for stream related events
 */
Client.prototype.makeUnaryRequest = function(method_descriptor, argument,
                                             metadata, options, callback) {
  /* While the arguments are listed in the function signature, those variables
   * are not used directly. Instead, ArgueJS processes the arguments
   * object. This allows for simple handling of optional arguments in the
   * middle of the argument list, and also provides type checking. */
  var args = arguejs({method_descriptor: MethodDescriptor, argument: null,
                      metadata: [Metadata, new Metadata()],
                      options: [Object, {}], callback: Function}, arguments);
  var constructor_interceptors = this[args.method_descriptor.name] ?
    this[args.method_descriptor.name].interceptors :
    null;
  args.options.method_descriptor = args.method_descriptor;
  var call_constructor = getCall.bind(null, this.$channel, method_descriptor);
  metadata = args.metadata.clone();
  var interceptors = client_interceptors.processInterceptorLayers(
    args.options,
    constructor_interceptors,
    args.method_descriptor
  );

  var batch_registry = new BatchRegistry();
  var intercepting_call = client_interceptors.getInterceptingCall(
    call_constructor, interceptors, batch_registry, args.options);
  var emitter = new ClientUnaryCall(intercepting_call);
  batches.registerBatches(emitter, batch_registry, [BATCH_TYPE.UNARY],
    args.options, args.callback);

  intercepting_call.start(metadata);
  intercepting_call.sendMessage(args.argument);
  intercepting_call.halfClose();

  return emitter;
};

/**
 * Make a client stream request to the given method, using the given method
 * descriptor, with the given argument.
 * @param {MethodDescriptor} method_descriptor A container of method attributes
 * @param {grpc.Metadata=} metadata Array of metadata key/value pairs to add to
 *     the call
 * @param {grpc.Client~CallOptions=} options Options map
 * @param {grpc.Client~requestCallback} callback The callback for when the
 *     response is received
 * @return {grpc~ClientWritableStream} An event emitter for stream related
 *     events
 */
Client.prototype.makeClientStreamRequest = function(method_descriptor, metadata,
                                                    options, callback) {
  /* While the arguments are listed in the function signature, those variables
   * are not used directly. Instead, ArgueJS processes the arguments
   * object. This allows for simple handling of optional arguments in the
   * middle of the argument list, and also provides type checking. */
  var args = arguejs({method_descriptor: MethodDescriptor,
                      metadata: [Metadata, new Metadata()],
                      options: [Object, {}], callback: Function}, arguments);
  var constructor_interceptors = this[args.method_descriptor.name] ?
    this[args.method_descriptor.name].interceptors :
    null;
  args.options.method_descriptor = args.method_descriptor;
  var interceptors = client_interceptors.processInterceptorLayers(
    args.options,
    constructor_interceptors,
    args.method_descriptor
  );
  var call_constructor = getCall.bind(null, this.$channel, method_descriptor);
  metadata = args.metadata.clone();

  var batch_registry = new BatchRegistry();
  var intercepting_call = client_interceptors.getInterceptingCall(
    call_constructor, interceptors, batch_registry, args.options);
  var emitter = new ClientWritableStream(intercepting_call);

  var batch_types = [
    BATCH_TYPE.METADATA,
    BATCH_TYPE.RECV_SYNC,
    BATCH_TYPE.SEND_STREAMING,
    BATCH_TYPE.CLOSE
  ];
  batches.registerBatches(emitter, batch_registry, batch_types, args.options,
    args.callback);

  intercepting_call.start(metadata);

  return emitter;
};

/**
 * Make a server stream request to the given method, using the given method
 * descriptor, with the given argument.
 * @param {MethodDescriptor} method_descriptor A container of method attributes
 * @param {*} argument The argument to the call. Should be serializable with
 *     serialize
 * @param {grpc.Metadata=} metadata Array of metadata key/value pairs to add to
 *     the call
 * @param {grpc.Client~CallOptions=} options Options map
 * @return {grpc~ClientReadableStream} An event emitter for stream related
 *     events
 */
Client.prototype.makeServerStreamRequest = function(method_descriptor, argument,
                                                    metadata, options) {
  /* While the arguments are listed in the function signature, those variables
   * are not used directly. Instead, ArgueJS processes the arguments
   * object. */
  var args = arguejs({method_descriptor: MethodDescriptor,
                      argument: null, metadata: [Metadata, new Metadata()],
                      options: [Object, {}]}, arguments);
  var constructor_interceptors = this[args.method_descriptor.name] ?
    this[args.method_descriptor.name].interceptors :
    null;
  args.options.method_descriptor = args.method_descriptor;
  var interceptors = client_interceptors.processInterceptorLayers(
    args.options,
    constructor_interceptors,
    args.method_descriptor
  );
  var call_constructor = getCall.bind(null, this.$channel, method_descriptor);
  metadata = args.metadata.clone();
  var batch_registry = new BatchRegistry();
  var intercepting_call = client_interceptors.getInterceptingCall(
    call_constructor, interceptors, batch_registry, args.options);

  var batch_types = [
    BATCH_TYPE.SEND_SYNC,
    BATCH_TYPE.STATUS,
    BATCH_TYPE.RECV_STREAMING
  ];

  var emitter = new ClientReadableStream(intercepting_call);
  batches.registerBatches(emitter, batch_registry, batch_types, args.options,
    args.callback);
  intercepting_call.start(metadata);
  intercepting_call.sendMessage(args.argument);
  intercepting_call.halfClose();
  return emitter;
};

/**
 * Make a bidirectional stream request with this method on the given channel.
 * @param {MethodDescriptor} method_descriptor A container of method attributes
 * @param {grpc.Metadata=} metadata Array of metadata key/value
 *     pairs to add to the call
 * @param {grpc.Client~CallOptions=} options Options map
 * @return {grpc~ClientDuplexStream} An event emitter for stream related events
 */
Client.prototype.makeBidiStreamRequest = function(method_descriptor, metadata,
                                                  options) {
  /* While the arguments are listed in the function signature, those variables
   * are not used directly. Instead, ArgueJS processes the arguments
   * object. */
  var args = arguejs({method_descriptor: MethodDescriptor,
                      metadata: [Metadata, new Metadata()],
                      options: [Object, {}]}, arguments);
  var constructor_interceptors = this[args.method_descriptor.name] ?
    this[args.method_descriptor.name].interceptors :
    null;
  args.options.method_descriptor = args.method_descriptor;
  var interceptors = client_interceptors.processInterceptorLayers(
    args.options,
    constructor_interceptors,
    args.method_descriptor
  );
  var call_constructor = getCall.bind(null, this.$channel, method_descriptor);
  metadata = args.metadata.clone();
  var batch_registry = new BatchRegistry();
  var intercepting_call = client_interceptors.getInterceptingCall(
    call_constructor, interceptors, batch_registry, args.options);

  var batch_types = [
    BATCH_TYPE.METADATA,
    BATCH_TYPE.STATUS,
    BATCH_TYPE.SEND_STREAMING,
    BATCH_TYPE.RECV_STREAMING,
    BATCH_TYPE.CLOSE
  ];

  var emitter = new ClientDuplexStream(intercepting_call);
  batches.registerBatches(emitter, batch_registry, batch_types, args.options,
    args.callback);
  intercepting_call.start(metadata);
  return emitter;
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
var requester_funcs = {};
requester_funcs[MethodType.UNARY] = Client.prototype.makeUnaryRequest;
requester_funcs[MethodType.CLIENT_STREAMING] =
  Client.prototype.makeClientStreamRequest;
requester_funcs[MethodType.SERVER_STREAMING] =
  Client.prototype.makeServerStreamRequest;
requester_funcs[MethodType.BIDI_STREAMING] =
  Client.prototype.makeBidiStreamRequest;

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
var deprecated_request_wrap = {};
deprecated_request_wrap[MethodType.UNARY] = function(makeUnaryRequest) {
  return function makeWrappedUnaryRequest(argument, callback,
                                          metadata, options) {
    /* jshint validthis: true */
    var opt_args = getDefaultValues(metadata, metadata);
    return makeUnaryRequest.call(this, argument, opt_args.metadata,
                                 opt_args.options, callback);
  };
};
deprecated_request_wrap[MethodType.CLIENT_STREAMING] =
  function(makeServerStreamRequest) {
    return function makeWrappedClientStreamRequest(callback, metadata,
                                                   options) {
      /* jshint validthis: true */
      var opt_args = getDefaultValues(metadata, options);
      return makeServerStreamRequest.call(this, opt_args.metadata,
                                          opt_args.options, callback);
    };
  };
deprecated_request_wrap[MethodType.SERVER_STREAMING] = _.identity;
deprecated_request_wrap[MethodType.BIDI_STREAMING] = _.identity;

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

  ServiceClient.prototype.$method_descriptors = [];
  util.inherits(ServiceClient, Client);

  _.each(methods, function(attrs, name) {
    var method_type;
    if (_.startsWith(name, '$')) {
      throw new Error('Method names cannot start with $');
    }
    if (attrs.requestStream) {
      if (attrs.responseStream) {
        method_type = MethodType.BIDI_STREAMING;
      } else {
        method_type = MethodType.CLIENT_STREAMING;
      }
    } else {
      if (attrs.responseStream) {
        method_type = MethodType.SERVER_STREAMING;
      } else {
        method_type = MethodType.UNARY;
      }
    }
    var serialize = attrs.requestStream ?
      common.wrapIgnoreNull(attrs.requestSerialize) :
      attrs.requestSerialize;
    var deserialize = attrs.responseStream ?
      common.wrapIgnoreNull(attrs.responseDeserialize) :
      attrs.responseDeserialize;
    var service_name = attrs.path.split('/')[1];
    var method_descriptor = new MethodDescriptor(name, service_name, attrs.path,
      method_type, serialize, deserialize);
    var method_func =
      _.partial(requester_funcs[method_type], method_descriptor);
    if (class_options.deprecatedArgumentOrder) {
      var request_wrap = deprecated_request_wrap[method_type];
      ServiceClient.prototype[name] = request_wrap(method_func);
    } else {
      ServiceClient.prototype[name] = method_func;
    }
    ServiceClient.prototype.$method_descriptors.push(method_descriptor);
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
 * Gets a map of client methods to interceptor arrays
 * @param {object} client
 * @returns {object}
 */
exports.getClientInterceptors = function(client) {
  return _.mapValues(_.keyBy(client.$method_descriptors, 'name'),
    function(d, name) {
      return client[name].interceptors;
    });
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

/**
 * Map of status code names to status codes
 */
exports.status = constants.status;

/**
 * See docs for client.callError
 */
exports.callError = grpc.callError;

exports.StatusBuilder = client_interceptors.StatusBuilder;
exports.ListenerBuilder = client_interceptors.ListenerBuilder;
exports.RequesterBuilder = client_interceptors.RequesterBuilder;
exports.InterceptingCall = client_interceptors.InterceptingCall;
exports.InterceptorProvider = client_interceptors.InterceptorProvider;
