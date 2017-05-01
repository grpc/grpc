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
 * Client Interceptors
 *
 * This module describes the interceptor framework for clients.
 * An interceptor is a function which takes an options object and a nextCall
 * function and returns an InterceptingCall:
 *
 * var interceptor = function(options, nextCall) {
 *   return new InterceptingCall(nextCall(options));
 * }
 *
 * The interceptor function must return an InterceptingCall object. Returning
 * `new InterceptingCall(nextCall(options))` will satisfy the contract (but
 * provide no interceptor functionality). `nextCall` is a function which will
 * run the next interceptor in the chain.
 *
 * To implement interceptor functionality, create a requester and pass it to
 * the InterceptingCall constructor:
 *
 * return new InterceptingCall(nextCall(options), requester);
 *
 * A requester is a POJO with zero or more of the following methods:
 *
 * `start(metadata, listener, next)`
 * * To continue, call next(metadata, listener). Listeners are described
 * * below.
 *
 * `sendMessage(message, next)`
 * * To continue, call next(message).
 *
 * `halfClose(next)`
 * * To continue, call next().
 *
 * `cancel(message, next)`
 * * To continue, call next().
 *
 * A listener is a POJO with one or more of the following methods:
 *
 * `onReceiveMetadata(metadata, next)`
 * * To continue, call next(metadata)
 *
 * `onReceiveMessage(message, next)`
 * * To continue, call next(message)
 *
 * `onReceiveStatus(status, next)`
 * * To continue, call next(status)
 *
 * A listener is passed into the requester's `start` method. This provided
 * listener implements all the inbound interceptor methods, which can be called
 * to short-circuit the gRPC call.
 *
 * Three usage patterns are supported for listeners:
 * 1) Pass the listener along without modification: `next(metadata, listener)`.
 *   In this case the interceptor declines to intercept any inbound operations.
 * 2) Create a new listener with one or more inbound interceptor methods and
 *   pass it to `next`. In this case the interceptor will fire on the inbound
 *   operations implemented in the new listener.
 * 3) Store the listener to make direct inbound calls on later. This effectively
 *   short-circuits the interceptor stack.
 *
 * Do not modify the listener passed in. Either pass it along unmodified or call
 * methods on it to short-circuit the interceptor stack.
 *
 * To intercept errors, implement the `onReceiveStatus` method and test for
 * `status.code !== grpc.status.OK`.
 *
 * To intercept trailers, examine `status.metadata` in the `onReceiveStatus`
 * method.
 *
 * This is a trivial implementation of all interceptor methods:
 * var interceptor = function(options, nextCall) {
 *   return new InterceptingCall(nextCall(options), {
 *     start: function(metadata, listener, next) {
 *       next(metadata, {
 *         onReceiveMetadata: function (metadata, next) {
 *           next(metadata);
 *         },
 *         onReceiveMessage: function (message, next) {
 *           next(message);
 *         },
 *         onReceiveStatus: function (status, next) {
 *           next(status);
 *         },
 *       });
 *     },
 *     sendMessage: function(message, next) {
 *       next(message);
 *     },
 *     halfClose: function(next) {
 *       next();
 *     },
 *     cancel: function(message, next) {
 *       next();
 *     }
 *   });
 * };
 *
 * This is an interceptor with a single method:
 * var interceptor = function(options, nextCall) {
 *   return new InterceptingCall(nextCall(options), {
 *     sendMessage: function(message, next) {
 *       next(message);
 *     }
 *   });
 * };
 *
 * Builders are provided for convenience: StatusBuilder, ListenerBuilder,
 * and RequesterBuilder
 *
 * gRPC client operations use this mapping to interceptor methods:
 *
 * grpc.opType.SEND_INITIAL_METADATA -> start
 * grpc.opType.SEND_MESSAGE -> sendMessage
 * grpc.opType.SEND_CLOSE_FROM_CLIENT -> halfClose
 * grpc.opType.RECV_INITIAL_METADATA -> onReceiveMetadata
 * grpc.opType.RECV_MESSAGE -> onReceiveMessage
 * grpc.opType.RECV_STATUS_ON_CLIENT -> onReceiveStatus
 *
 * @module
 */

'use strict';

var _ = require('lodash');
var grpc = require('./grpc_extension');

var OUTBOUND_OPS = [
  grpc.opType.SEND_INITIAL_METADATA,
  grpc.opType.SEND_MESSAGE,
  grpc.opType.SEND_CLOSE_FROM_CLIENT
];

var INBOUND_OPS = [
  grpc.opType.RECV_INITIAL_METADATA,
  grpc.opType.RECV_MESSAGE,
  grpc.opType.RECV_STATUS_ON_CLIENT
];

/**
 * A custom error thrown when interceptor configuration fails
 * @param {string} message The error message
 * @param {object} [extra]
 * @constructor
 */
var InterceptorConfigurationError =
  function InterceptorConfigurationError(message, extra) {
    Error.captureStackTrace(this, this.constructor);
    this.name = this.constructor.name;
    this.message = message;
    this.extra = extra;
  };

require('util').inherits(InterceptorConfigurationError, Error);

/**
 * A builder for gRPC status objects
 * @constructor
 */
function StatusBuilder() {
  this.code = null;
  this.details = null;
  this.metadata = null;
}

/**
 * Adds a status code to the builder
 * @param {number} code The status code
 * @return {StatusBuilder}
 */
StatusBuilder.prototype.withCode = function(code) {
  this.code = code;
  return this;
};

/**
 * Adds details to the builder
 * @param {string} details A status message
 * @return {StatusBuilder}
 */
StatusBuilder.prototype.withDetails = function(details) {
  this.details = details;
  return this;
};

/**
 * Adds metadata to the builder
 * @param {Metadata} metadata The gRPC status metadata
 * @return {StatusBuilder}
 */
StatusBuilder.prototype.withMetadata = function(metadata) {
  this.metadata = metadata;
  return this;
};

/**
 * Builds the status object
 * @return {object} A gRPC status
 */
StatusBuilder.prototype.build = function() {
  var status = {};
  if (this.code !== undefined) {
    status.code = this.code;
  }
  if (this.details) {
    status.details = this.details;
  }
  if (this.metadata) {
    status.metadata = this.metadata;
  }
  return status;
};

/**
 * A builder for listener interceptors
 * @constructor
 */
function ListenerBuilder() {
  this.metadata = null;
  this.message = null;
  this.status = null;
}

/**
 * Adds an onReceiveMetadata method to the builder
 * @param {Function} on_receive_metadata A listener method for receiving
 * metadata
 * @return {ListenerBuilder}
 */
ListenerBuilder.prototype.withOnReceiveMetadata =
  function(on_receive_metadata) {
    this.metadata = on_receive_metadata;
    return this;
  };

/**
 * Adds an onReceiveMessage method to the builder
 * @param {Function} on_receive_message A listener method for receiving messages
 * @return {ListenerBuilder}
 */
ListenerBuilder.prototype.withOnReceiveMessage = function(on_receive_message) {
  this.message = on_receive_message;
  return this;
};

/**
 * Adds an onReceiveStatus method to the builder
 * @param {Function} on_receive_status A listener method for receiving status
 * @return {ListenerBuilder}
 */
ListenerBuilder.prototype.withOnReceiveStatus = function(on_receive_status) {
  this.status = on_receive_status;
  return this;
};

/**
 * Builds the call listener
 * @return {object}
 */
ListenerBuilder.prototype.build = function() {
  var self = this;
  var listener = {};
  listener.onReceiveMetadata = self.metadata;
  listener.onReceiveMessage = self.message;
  listener.onReceiveStatus = self.status;
  return listener;
};

/**
 * A builder for the outbound methods of an interceptor
 * @constructor
 */
function RequesterBuilder() {
  this.start = null;
  this.message = null;
  this.half_close = null;
  this.cancel = null;
}

/**
 * Add a `start` interceptor
 * @param {Function} start A requester method for handling `start`
 * @return {RequesterBuilder}
 */
RequesterBuilder.prototype.withStart = function(start) {
  this.start = start;
  return this;
};

/**
 * Add a `sendMessage` interceptor
 * @param {Function} send_message A requester method for handling `sendMessage`
 * @return {RequesterBuilder}
 */
RequesterBuilder.prototype.withSendMessage = function(send_message) {
  this.message = send_message;
  return this;
};

/**
 * Add a `halfClose` interceptor
 * @param {Function} half_close A requester method for handling `halfClose`
 * @return {RequesterBuilder}
 */
RequesterBuilder.prototype.withHalfClose = function(half_close) {
  this.half_close = half_close;
  return this;
};

/**
 * Add a `cancel` interceptor
 * @param {Function} cancel A requester method for handling `cancel`
 * @return {RequesterBuilder}
 */
RequesterBuilder.prototype.withCancel = function(cancel) {
  this.cancel = cancel;
  return this;
};

/**
 * Builds the requester's interceptor methods
 * @return {object}
 */
RequesterBuilder.prototype.build = function() {
  var interceptor = {};
  interceptor.start = this.start;
  interceptor.sendMessage = this.message;
  interceptor.halfClose = this.half_close;
  interceptor.cancel = this.cancel;
  return interceptor;
};

/**
 * A simple type for determining whether an interceptor applies to a given gRPC
 * method.
 * @param {function} get_interceptor Takes a MethodDescriptor and returns an
 * interceptor (if it applies to the method)
 * @constructor
 */
var InterceptorProvider = function(get_interceptor) {
  this.get_interceptor = get_interceptor;
};

/**
 * Determines if an interceptor applies to a given method
 * @param {MethodDescriptor} method_descriptor
 * @returns {function|undefined} Either an interceptor function, or a falsey
 * value indicating the interceptor does not apply.
 */
InterceptorProvider.prototype.getInterceptorForMethod =
  function(method_descriptor) {
    return this.get_interceptor(method_descriptor);
  };

/**
 * Transforms a list of interceptor providers into interceptors
 * @param {object[]} providers The interceptor providers
 * @param {MethodDescriptor} method_descriptor
 * @return {null|function[]} An array of interceptors or null
 */
var resolveInterceptorProviders = function(providers, method_descriptor) {
  if (!_.isArray(providers)) {
    return null;
  }
  return _.flatMap(providers, function(provider) {
    if (!_.isFunction(provider.getInterceptorForMethod)) {
      throw new InterceptorConfigurationError(
        'InterceptorProviders must implement `getInterceptorForMethod`');
    }
    var interceptor = provider.getInterceptorForMethod(method_descriptor);
    return interceptor ? [interceptor] : [];
  });
};

/**
 * Resolves interceptor options at call invocation time
 * @param {object} options The call options passed to a gRPC call
 * @param {function[]} [options.interceptors] An array of interceptors
 * @param {object[]} [options.interceptor_providers] An array of providers
 * @param {MethodDescriptor} method_descriptor
 * @return {null|function[]} The resulting interceptors
 */
var resolveInterceptorOptions = function(options, method_descriptor) {
  var provided = resolveInterceptorProviders(options.interceptor_providers,
    method_descriptor);
  var interceptor_options = [
    options.interceptors,
    provided
  ];
  var too_many_options = _.every(interceptor_options, function(interceptors) {
    return _.isArray(interceptors);
  });
  if (too_many_options) {
    throw new InterceptorConfigurationError(
      'Both interceptors and interceptor_providers were passed as options ' +
      'to the call invocation. Only one of these is allowed.');
  }
  return _.find(interceptor_options, function(interceptors) {
      return _.isArray(interceptors);
    }) || null;
};

/**
 * Process call options and the interceptor override layers to get the final set
 * of interceptors
 * @param {object} call_options The options passed to the gRPC call
 * @param {function[]} constructor_interceptors Interceptors passed to the
 * client constructor
 * @param {MethodDescriptor} method_descriptor Details of the gRPC call method
 * @return {Function[]|null} The final set of interceptors
 */
var processInterceptorLayers = function(call_options,
                                        constructor_interceptors,
                                        method_descriptor) {
  var calltime_interceptors = resolveInterceptorOptions(call_options,
    method_descriptor);
  var interceptor_overrides = [
    calltime_interceptors,
    constructor_interceptors
  ];
  return _resolveInterceptorOverrides(interceptor_overrides);
};

/**
 * A chain-able gRPC call proxy which will delegate to an optional requester
 * object. By default, interceptor methods will chain to next_call. If a
 * requester is provided which implements an interceptor method, that
 * requester method will be executed as part of the chain.
 * @param {InterceptingCall|null} next_call The next call in the chain
 * @param {object} [requester] An object containing optional delegate methods
 * @constructor
 */
function InterceptingCall(next_call, requester) {
  _validateRequester(requester);
  this.next_call = next_call;
  this.requester = requester;
}

/**
 * Get the next method in the chain or a no-op function if we are at the end
 * of the chain
 * @param {string} method_name
 * @return {Function} The next method in the chain
 * @private
 */
InterceptingCall.prototype._getNextCall = function(method_name) {
  return this.next_call ?
    this.next_call[method_name].bind(this.next_call) :
    function(){};
};

/**
 * Call the next method in the chain. This will either be on the next
 * InterceptingCall (next_call), or the requester if the requester
 * implements the method.
 * @param {string} method_name The name of the interceptor method
 * @param {array} [args] Payload arguments for the operation
 * @param {function} [next] The next InterceptingCall's method
 * @return {*}
 * @private
 */
InterceptingCall.prototype._callNext = function(method_name, args, next) {
  var args_array = args || [];
  var next_call = next ? next : this._getNextCall(method_name);
  if (this.requester && this.requester[method_name]) {
    var delegate_args = _.concat(args_array, next_call);
    return this.requester[method_name].apply(this.requester, delegate_args);
  } else {
    return next_call.apply(null, args_array);
  }
};

/**
 * Starts a call through the outbound interceptor chain and adds an element to
 * the reciprocal inbound listener chain.
 * @param {Metadata} metadata The outgoing metadata
 * @param {object} listener An intercepting listener for inbound ops
 */
InterceptingCall.prototype.start = function(metadata, listener) {
  var self = this;

  _validateListener(listener);

  // If the listener provided is an InterceptingListener, use it. Otherwise, we
  // must be at the end of the listener chain, and any listener operations
  // should be terminated in an EndListener.
  var next_listener = _getInterceptingListener(listener, new EndListener());

  // Build the next method in the interceptor chain
  var next = function(metadata, current_listener) {
    // If there is a next call in the chain, run it. Otherwise do nothing.
    if (self.next_call) {
      // Wire together any listener provided with the next listener
      var listener = _getInterceptingListener(current_listener, next_listener);
      self.next_call.start(metadata, listener);
    }
  };
  this._callNext('start', [metadata, next_listener], next);
};

/**
 * Pass a message through the interceptor chain
 * @param {object} message
 */
InterceptingCall.prototype.sendMessage = function(message) {
  this._callNext('sendMessage', [message]);
};

/**
 * Run a close operation through the interceptor chain
 */
InterceptingCall.prototype.halfClose = function() {
  this._callNext('halfClose');
};

/**
 * Run a cancel operation through the interceptor chain
 */
InterceptingCall.prototype.cancel = function() {
  this._callNext('cancel');
};

/**
 * Run a cancelWithStatus operation through the interceptor chain
 * @param {object} status
 * @param {string} message
 */
InterceptingCall.prototype.cancelWithStatus = function(status, message) {
  this._callNext('cancelWithStatus', [status, message]);
};

/**
 * Pass a getPeer call down to the base gRPC call (should not be intercepted)
 * @return {object}
 */
InterceptingCall.prototype.getPeer = function() {
  return this._callNext('getPeer');
};

/**
 * For streaming calls, we need to transparently pass the stream's context
 * through the interceptor chain. Passes the context between InterceptingCalls
 * but hides it from any requester implementations.
 * @param {object} context Carries objects needed for streaming operations
 * @param {object} message The message to send
 */
InterceptingCall.prototype.sendMessageWithContext = function(context, message) {
  var next = this.next_call ?
    this.next_call.sendMessageWithContext.bind(this.next_call, context) :
    context;
  this._callNext('sendMessage', [message], next);
};

/**
 * For receiving streaming messages, we need to seed the base interceptor with
 * the streaming context to create a RECV_MESSAGE batch.
 * @param {object} context Carries objects needed for streaming operations
 */
InterceptingCall.prototype.recvMessageWithContext = function(context) {
  this._callNext('recvMessageWithContext', [context]);
};

/**
 * A chain-able listener object which will delegate to a custom listener when
 * appropriate.
 * @param {InterceptingListener|null} next_listener The next
 * InterceptingListener in the chain
 * @param {object|null} delegate A custom listener object which may implement
 * specific operations
 * @constructor
 */
function InterceptingListener(next_listener, delegate) {
  this.delegate = delegate || {};
  this.next_listener = next_listener;
}

/**
 * Get the next method in the chain or a no-op function if we are at the end
 * of the chain.
 * @param {string} method_name
 * @return {Function} The next method in the chain
 * @private
 */
InterceptingListener.prototype._getNextListener = function(method_name) {
  return this.next_listener ?
    this.next_listener[method_name].bind(this.next_listener) :
    function(){};
};

/**
 * Call the next method in the chain. This will either be on the next
 * InterceptingListener (next_listener), or the requester if the requester
 * implements the method.
 * @param {string} method_name The name of the interceptor method
 * @param {array} [args] Payload arguments for the operation
 * @param {function} [next] The next InterceptingListener's method
 * @return {*}
 * @private
 */
InterceptingListener.prototype._callNext = function(method_name, args, next) {
  var args_array = args || [];
  var next_listener = next ? next : this._getNextListener(method_name);
  if (this.delegate && this.delegate[method_name]) {
    var delegate_args = _.concat(args_array, next_listener);
    return this.delegate[method_name].apply(this.delegate, delegate_args);
  } else {
    return next_listener.apply(null, args_array);
  }
};
/**
 * Inbound metadata receiver
 * @param {Metadata} metadata
 */
InterceptingListener.prototype.onReceiveMetadata = function(metadata) {
  this._callNext('onReceiveMetadata', [metadata]);
};

/**
 * Inbound message receiver
 * @param {object} message
 */
InterceptingListener.prototype.onReceiveMessage = function(message) {
  this._callNext('onReceiveMessage', [message]);
};

/**
 * When intercepting streaming message, we need to pass the streaming context
 * transparently along the chain. Hides the context from the delegate listener
 * methods.
 * @param {object} context Carries objects needed for streaming operations
 * @param {object} message The message received
 */
InterceptingListener.prototype.recvMessageWithContext = function(context,
                                                                 message) {
  var fallback = this.next_listener.recvMessageWithContext;
  var next_method = this.next_listener ?
    fallback.bind(this.next_listener, context) :
    context;
  if (this.delegate.onReceiveMessage) {
    this.delegate.onReceiveMessage(message, next_method, context);
  } else {
    next_method(message);
  }
};

/**
 * Inbound status receiver
 * @param {object} status
 */
InterceptingListener.prototype.onReceiveStatus = function(status) {
  this._callNext('onReceiveStatus', [status]);
};

/**
 * A dead-end listener used to terminate a call chain. Used when an interceptor
 * creates a branch chain, when the branch returns the listener chain will
 * terminate here.
 * @constructor
 */
function EndListener() {}
EndListener.prototype.onReceiveMetadata = function(){};
EndListener.prototype.onReceiveMessage = function(){};
EndListener.prototype.onReceiveStatus = function(){};

/**
 * Creates a proxy for a gRPC call object which routes all operations through
 * a chain of interceptors.
 * @param {function} call_constructor Used to create the underlying gRPC call
 * @param {function[]} interceptors A list of interceptors in order
 * @param {BatchRegistry} registry A registry of the client batches used in the
 * call
 * @param {object} options The call options
 * @returns {InterceptingCall}
 */
function getInterceptingCall(call_constructor, interceptors, registry,
                              options) {
  var interceptor_base = _getOutboundBatchingInterceptor(
    call_constructor, registry);
  var interceptor_top = _getInboundBatchingInterceptor(
    registry);
  var all_interceptors = _.concat(interceptor_top, interceptors,
    interceptor_base);
  return _chainInterceptors(all_interceptors, options);
}


/**
 * Returns a base interceptor for outbound operations which will construct
 * the underlying gRPC call and run startBatch on it after the batch's
 * operations have passed through the interceptor chain.
 * @param {function} call_constructor
 * @param {BatchRegistry} registry
 * @return {Function}
 */
function _getOutboundBatchingInterceptor(call_constructor, registry) {
  return function(options) {
    var ops_received = {};
    var response_listener;
    var call = call_constructor(options);
    var handler = function(batch, handle, context) {
      handle(batch, call, response_listener, context);
    };
    return new InterceptingCall(null, {
      start: function (metadata, listener) {
        response_listener = listener;
        var op = grpc.opType.SEND_INITIAL_METADATA;
        ops_received = _handleOutboundOperations(op, metadata, ops_received,
          registry, handler);
      },
      sendMessage: function (message, context) {
        var op = grpc.opType.SEND_MESSAGE;
        ops_received = _handleOutboundOperations(op, message, ops_received,
          registry, handler, context);
      },
      halfClose: function () {
        var op = grpc.opType.SEND_CLOSE_FROM_CLIENT;
        ops_received = _handleOutboundOperations(op, null, ops_received,
          registry, handler);
      },
      recvMessageWithContext: function(context) {
        var op = grpc.opType.RECV_MESSAGE;
        _handleOutboundOperations(op, null, ops_received, registry, handler,
          context);
      },
      cancel: function() {
        call.cancel();
      },
      cancelWithStatus: function(status, message) {
        call.cancelWithStatus(status, message);
      },
      getPeer: function() {
        return call.getPeer();
      }
    });
  };
}

/**
 * Creates a base inbound interceptor which will run callbacks for each batch
 * after their inbound operations have passed through the interceptor chain.
 * @param {BatchRegistry} registry
 * @return {Function}
 */
function _getInboundBatchingInterceptor(registry) {
  return function(options, nextCall) {
    var ops_received = {};
    var handler = function(batch, handle, context) {
      handle(batch, context);
    };
    return new InterceptingCall(nextCall(options), {
      start: function (metadata, listener, next) {
        next(metadata, {
          onReceiveMetadata: function (metadata) {
            var op = grpc.opType.RECV_INITIAL_METADATA;
            ops_received = _handleInboundOperations(op, metadata, ops_received,
              registry, handler);
          },
          onReceiveMessage: function (message, next, context) {
            var op = grpc.opType.RECV_MESSAGE;
            ops_received = _handleInboundOperations(op, message, ops_received,
              registry, handler, context);
          },
          onReceiveStatus: function (status) {
            var op = grpc.opType.RECV_STATUS_ON_CLIENT;
            ops_received = _handleInboundOperations(op, status, ops_received,
              registry, handler);
          },
          recvMessageWithContext: function(context, message) {
            var op = grpc.opType.RECV_MESSAGE;
            _handleInboundOperations(op, message, ops_received, registry,
              handler, context);
          },
        });
      }
    });
  };
}

/**
 * Chain a list of interceptors together and return the top InterceptingCall
 * @param {function[]} interceptors
 * @param {object} options Call options
 * @return {InterceptingCall}
 */
function _chainInterceptors(interceptors, options) {
  var newCall = function(interceptors) {
    if (interceptors.length === 0) {
      return function(options) {};
    }
    var head_interceptor = _.head(interceptors);
    var rest_interceptors = _.tail(interceptors);
    return function(options) {
      return head_interceptor(options, newCall(rest_interceptors));
    };
  };
  var nextCall = newCall(interceptors)(options);
  return new InterceptingCall(nextCall);
}

/**
 * Wraps a plain listener object in an InterceptingListener if it isn't an
 * InterceptingListener already.
 * @param {InterceptingListener|object|null} current_listener
 * @param {InterceptingListener|EndListener} next_listener
 * @return {InterceptingListener|null}
 * @private
 */
function _getInterceptingListener(current_listener, next_listener) {
  if (!_isInterceptingListener(current_listener)) {
    return new InterceptingListener(next_listener, current_listener);
  }
  return current_listener;
}

/**
 * Test if the listener exists and is an InterceptingListener
 * @param listener
 * @return {boolean}
 * @private
 */
function _isInterceptingListener(listener) {
  return listener && listener.constructor.name === 'InterceptingListener';
}

/**
 * Checks that methods attached to an inbound interceptor match the API
 * @param {object} listener An interceptor listener
 * @private
 */
function _validateListener(listener) {
  var inbound_methods = [
    'onReceiveMetadata',
    'onReceiveMessage',
    'onReceiveStatus'
  ];
  _.forOwn(listener, function(value, key) {
    if (!_.includes(inbound_methods, key) && _.isFunction(value)) {
      var message = key + ' is not a valid interceptor listener method. ' +
        'Valid methods: ' + JSON.stringify(inbound_methods);
      throw new InterceptorConfigurationError(message);
    }
  });
}

/**
 * Checks that methods attached to a requester match the API
 * @param {object} requester A candidate interceptor requester
 * @private
 */
function _validateRequester(requester) {
  var outbound_methods = [
    'start',
    'sendMessage',
    'halfClose',
    'cancel',
    'cancelWithStatus'
  ];
  var internal_methods = [
    'getPeer',
    'recvMessageWithContext'
  ];
  var valid = function(key) {
    return _.includes(outbound_methods, key) ||
      _.includes(internal_methods, key);
  };
  _.forOwn(requester, function(value, key) {
    if (!valid(key)) {
      var message = key + ' is not a valid interceptor requester method. ' +
        'Valid methods: ' + JSON.stringify(outbound_methods);
      throw new InterceptorConfigurationError(message);
    }
  });
}

/**
 * Filters a list of operations
 * @param {number[]} op_candidates A list to filter
 * @param {number[]} ops_allowed The allowed operations
 * @return {number[]}
 * @private
 */
function _filterOps(op_candidates, ops_allowed) {
  return _.filter(op_candidates, function(op) {
    return _.includes(ops_allowed, op);
  });
}

/**
 * Returns only the inbound operations from a list of ops
 * @param {number[]} ops The operations to filter
 * @return {number[]}
 * @private
 */
function _getInboundOps(ops) {
  return _filterOps(ops, INBOUND_OPS);
}

/**
 * Returns only the outbound operations from a list of ops
 * @param {number[]} ops The operations to filter
 * @return {number[]}
 * @private
 */
function _getOutboundOps(ops) {
  return _filterOps(ops, OUTBOUND_OPS);
}

/**
 * For each batch, run a handler function
 * @param {BatchDefinition[]} batch_definitions Batch definitions to handle
 * @param {object} ops_received The completed operations
 * @param {boolean} is_outbound Whether to handle inbound or outbound operations
 * @param {function} handler A closure with the values needed to handle
 * the batch
 * @param {object} [context] The optional streaming context object
 * @private
 */
function _handleBatches(batch_definitions, ops_received, is_outbound,
                           handler, context) {
  _.each(batch_definitions, function(definition) {
    var batch_values = _.map(definition.batch_ops, function(op) {
      return ops_received[op];
    });
    var batch = _.zipObject(definition.batch_ops, batch_values);
    var base_handler = definition.getHandler(is_outbound);
    handler(batch, base_handler, context);
  });
}

/**
 * Record the completion of an operation's interceptor chain and the result
 * value
 * @param {number} op
 * @param {*} op_value
 * @param {object} ops_received
 * @return {object}
 * @private
 */
function _saveOperation(op, op_value, ops_received) {
  if (op !== null && op !== undefined) {
    ops_received[op] = op_value;
  }
  return ops_received;
}

/**
 * Find the batches for which all their operations have completed their
 * interception chains and start or resolve them.
 * @param {number} op The operation just completed
 * @param {*} op_value The operation's value
 * @param {object} ops_received An aggregation of all operations completed
 * @param {BatchRegistry} registry The batch registry
 * @param {boolean} is_outbound Whether to handle inboud or outbound operations
 * @param {function} handler A closure with the values needed to handle
 * the batch.
 * @param {object} [context] An optional streaming context
 * @return {Object}
 * @private
 */
function _handleOperations(op, op_value, ops_received, registry, is_outbound,
                          handler, context) {
  ops_received = _saveOperation(op, op_value, ops_received);
  var ops_complete = _.map(_.keys(ops_received), function(op) {
    return parseInt(op);
  });

  var op_filter = is_outbound ? _getOutboundOps : _getInboundOps;
  var batches = _.filter(registry.batches, function(batch) {
    // Skip the batch if this operation cannot trigger the handler
    if (!_.includes(batch.trigger_ops, op)) {
      return false;
    }
    // Skip the batch if it has no handler
    if (!batch.getHandler(is_outbound)) {
      return false;
    }
    // The batch is complete if all the required operations are complete
    var ops_required = op_filter(batch.batch_ops);
    var required_and_complete = _.intersection(ops_complete, ops_required);
    return _.isEqual(required_and_complete, ops_required);
  });

  _handleBatches(batches, ops_received, is_outbound, handler, context);
  return ops_received;
}

/**
 * Starts batches whose outbound operations have completed their interceptor
 * chains.
 * @param {number} op The operation just completed
 * @param {*} value The value of the operation just completed
 * @param {object} ops_received An aggregation of all operations completed
 * @param {BatchRegistry} registry The batch registry
 * @param {function} handler A closure with the values needed to handle
 * the batch
 * @param {object} [context] An optional streaming context
 * @returns {Object}
 * @private
 */
function _handleOutboundOperations(op, value, ops_received, registry,
                                 handler, context) {
  return _handleOperations(op, value, ops_received, registry, true, handler,
    context);
}

/**
 * Resolves batches whose inbound operations have completed their interceptor
 * chains.
 * @param {number} op The operation just completed
 * @param {*} value The value of the operation just completed
 * @param {object} ops_received An aggregation of all operations completed
 * @param {BatchRegistry} registry The batch registry
 * @param {function} handler A closure with the values needed to handle
 * the batch
 * @param {object} [context] An optional streaming context
 * @returns {Object}
 * @private
 */
function _handleInboundOperations(op, value, ops_received, registry,
                                handler, context) {
  return _handleOperations(op, value, ops_received, registry, false, handler,
    context);
}

/**
 * Chooses the first valid array of interceptors or returns null
 * @param {function[][]} interceptor_lists A list of interceptor lists in
 * descending override priority order
 * @return {function[]|null} The resulting interceptors
 * @private
 */
function _resolveInterceptorOverrides(interceptor_lists) {
  return _.find(interceptor_lists, function(interceptor_list) {
      return _.isArray(interceptor_list);
    }) || null;
}

exports.processInterceptorLayers = processInterceptorLayers;
exports.resolveInterceptorProviders = resolveInterceptorProviders;

exports.InterceptorProvider = InterceptorProvider;
exports.InterceptingCall = InterceptingCall;
exports.ListenerBuilder = ListenerBuilder;
exports.RequesterBuilder = RequesterBuilder;
exports.StatusBuilder = StatusBuilder;

exports.InterceptorConfigurationError = InterceptorConfigurationError;

exports.getInterceptingCall = getInterceptingCall;

