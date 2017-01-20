# Copyright 2015-2016, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
"""gRPC's Python API."""

import abc
import enum
import sys

import six

from grpc._cython import cygrpc as _cygrpc

############################## Future Interface  ###############################


class FutureTimeoutError(Exception):
    """Indicates that a method call on a Future timed out."""


class FutureCancelledError(Exception):
    """Indicates that the computation underlying a Future was cancelled."""


class Future(six.with_metaclass(abc.ABCMeta)):
    """A representation of a computation in another control flow.

  Computations represented by a Future may be yet to be begun, may be ongoing,
  or may have already completed.
  """

    @abc.abstractmethod
    def cancel(self):
        """Attempts to cancel the computation.

    This method does not block.

    Returns:
      True if the computation has not yet begun, will not be allowed to take
        place, and determination of both was possible without blocking. False
        under all other circumstances including but not limited to the
        computation's already having begun, the computation's already having
        finished, and the computation's having been scheduled for execution on a
        remote system for which a determination of whether or not it commenced
        before being cancelled cannot be made without blocking.
    """
        raise NotImplementedError()

    @abc.abstractmethod
    def cancelled(self):
        """Describes whether the computation was cancelled.

    This method does not block.

    Returns:
      True if the computation was cancelled any time before its result became
        immediately available. False under all other circumstances including but
        not limited to this object's cancel method not having been called and
        the computation's result having become immediately available.
    """
        raise NotImplementedError()

    @abc.abstractmethod
    def running(self):
        """Describes whether the computation is taking place.

    This method does not block.

    Returns:
      True if the computation is scheduled to take place in the future or is
        taking place now, or False if the computation took place in the past or
        was cancelled.
    """
        raise NotImplementedError()

    @abc.abstractmethod
    def done(self):
        """Describes whether the computation has taken place.

    This method does not block.

    Returns:
      True if the computation is known to have either completed or have been
        unscheduled or interrupted. False if the computation may possibly be
        executing or scheduled to execute later.
    """
        raise NotImplementedError()

    @abc.abstractmethod
    def result(self, timeout=None):
        """Accesses the outcome of the computation or raises its exception.

    This method may return immediately or may block.

    Args:
      timeout: The length of time in seconds to wait for the computation to
        finish or be cancelled, or None if this method should block until the
        computation has finished or is cancelled no matter how long that takes.

    Returns:
      The return value of the computation.

    Raises:
      FutureTimeoutError: If a timeout value is passed and the computation does
        not terminate within the allotted time.
      FutureCancelledError: If the computation was cancelled.
      Exception: If the computation raised an exception, this call will raise
        the same exception.
    """
        raise NotImplementedError()

    @abc.abstractmethod
    def exception(self, timeout=None):
        """Return the exception raised by the computation.

    This method may return immediately or may block.

    Args:
      timeout: The length of time in seconds to wait for the computation to
        terminate or be cancelled, or None if this method should block until
        the computation is terminated or is cancelled no matter how long that
        takes.

    Returns:
      The exception raised by the computation, or None if the computation did
        not raise an exception.

    Raises:
      FutureTimeoutError: If a timeout value is passed and the computation does
        not terminate within the allotted time.
      FutureCancelledError: If the computation was cancelled.
    """
        raise NotImplementedError()

    @abc.abstractmethod
    def traceback(self, timeout=None):
        """Access the traceback of the exception raised by the computation.

    This method may return immediately or may block.

    Args:
      timeout: The length of time in seconds to wait for the computation to
        terminate or be cancelled, or None if this method should block until
        the computation is terminated or is cancelled no matter how long that
        takes.

    Returns:
      The traceback of the exception raised by the computation, or None if the
        computation did not raise an exception.

    Raises:
      FutureTimeoutError: If a timeout value is passed and the computation does
        not terminate within the allotted time.
      FutureCancelledError: If the computation was cancelled.
    """
        raise NotImplementedError()

    @abc.abstractmethod
    def add_done_callback(self, fn):
        """Adds a function to be called at completion of the computation.

    The callback will be passed this Future object describing the outcome of
    the computation.

    If the computation has already completed, the callback will be called
    immediately.

    Args:
      fn: A callable taking this Future object as its single parameter.
    """
        raise NotImplementedError()


################################  gRPC Enums  ##################################


@enum.unique
class ChannelConnectivity(enum.Enum):
    """Mirrors grpc_connectivity_state in the gRPC Core.

  Attributes:
    IDLE: The channel is idle.
    CONNECTING: The channel is connecting.
    READY: The channel is ready to conduct RPCs.
    TRANSIENT_FAILURE: The channel has seen a failure from which it expects to
      recover.
    SHUTDOWN: The channel has seen a failure from which it cannot recover.
  """
    IDLE = (_cygrpc.ConnectivityState.idle, 'idle')
    CONNECTING = (_cygrpc.ConnectivityState.connecting, 'connecting')
    READY = (_cygrpc.ConnectivityState.ready, 'ready')
    TRANSIENT_FAILURE = (_cygrpc.ConnectivityState.transient_failure,
                         'transient failure')
    SHUTDOWN = (_cygrpc.ConnectivityState.shutdown, 'shutdown')


@enum.unique
class StatusCode(enum.Enum):
    """Mirrors grpc_status_code in the gRPC Core."""
    OK = (_cygrpc.StatusCode.ok, 'ok')
    CANCELLED = (_cygrpc.StatusCode.cancelled, 'cancelled')
    UNKNOWN = (_cygrpc.StatusCode.unknown, 'unknown')
    INVALID_ARGUMENT = (_cygrpc.StatusCode.invalid_argument, 'invalid argument')
    DEADLINE_EXCEEDED = (_cygrpc.StatusCode.deadline_exceeded,
                         'deadline exceeded')
    NOT_FOUND = (_cygrpc.StatusCode.not_found, 'not found')
    ALREADY_EXISTS = (_cygrpc.StatusCode.already_exists, 'already exists')
    PERMISSION_DENIED = (_cygrpc.StatusCode.permission_denied,
                         'permission denied')
    RESOURCE_EXHAUSTED = (_cygrpc.StatusCode.resource_exhausted,
                          'resource exhausted')
    FAILED_PRECONDITION = (_cygrpc.StatusCode.failed_precondition,
                           'failed precondition')
    ABORTED = (_cygrpc.StatusCode.aborted, 'aborted')
    OUT_OF_RANGE = (_cygrpc.StatusCode.out_of_range, 'out of range')
    UNIMPLEMENTED = (_cygrpc.StatusCode.unimplemented, 'unimplemented')
    INTERNAL = (_cygrpc.StatusCode.internal, 'internal')
    UNAVAILABLE = (_cygrpc.StatusCode.unavailable, 'unavailable')
    DATA_LOSS = (_cygrpc.StatusCode.data_loss, 'data loss')
    UNAUTHENTICATED = (_cygrpc.StatusCode.unauthenticated, 'unauthenticated')


#############################  gRPC Exceptions  ################################


class RpcError(Exception):
    """Raised by the gRPC library to indicate non-OK-status RPC termination."""


##############################  Shared Context  ################################


class RpcContext(six.with_metaclass(abc.ABCMeta)):
    """Provides RPC-related information and action."""

    @abc.abstractmethod
    def is_active(self):
        """Describes whether the RPC is active or has terminated."""
        raise NotImplementedError()

    @abc.abstractmethod
    def time_remaining(self):
        """Describes the length of allowed time remaining for the RPC.

    Returns:
      A nonnegative float indicating the length of allowed time in seconds
      remaining for the RPC to complete before it is considered to have timed
      out, or None if no deadline was specified for the RPC.
    """
        raise NotImplementedError()

    @abc.abstractmethod
    def cancel(self):
        """Cancels the RPC.

    Idempotent and has no effect if the RPC has already terminated.
    """
        raise NotImplementedError()

    @abc.abstractmethod
    def add_callback(self, callback):
        """Registers a callback to be called on RPC termination.

    Args:
      callback: A no-parameter callable to be called on RPC termination.

    Returns:
      True if the callback was added and will be called later; False if the
        callback was not added and will not later be called (because the RPC
        already terminated or some other reason).
    """
        raise NotImplementedError()


#########################  Invocation-Side Context  ############################


class Call(six.with_metaclass(abc.ABCMeta, RpcContext)):
    """Invocation-side utility object for an RPC."""

    @abc.abstractmethod
    def initial_metadata(self):
        """Accesses the initial metadata from the service-side of the RPC.

    This method blocks until the value is available.

    Returns:
      The initial :term:`metadata`.
    """
        raise NotImplementedError()

    @abc.abstractmethod
    def trailing_metadata(self):
        """Accesses the trailing metadata from the service-side of the RPC.

    This method blocks until the value is available.

    Returns:
      The trailing :term:`metadata`.
    """
        raise NotImplementedError()

    @abc.abstractmethod
    def code(self):
        """Accesses the status code emitted by the service-side of the RPC.

    This method blocks until the value is available.

    Returns:
      The StatusCode value for the RPC.
    """
        raise NotImplementedError()

    @abc.abstractmethod
    def details(self):
        """Accesses the details value emitted by the service-side of the RPC.

    This method blocks until the value is available.

    Returns:
      The details string of the RPC.
    """
        raise NotImplementedError()


############  Authentication & Authorization Interfaces & Classes  #############


class ChannelCredentials(object):
    """A value encapsulating the data required to create a secure Channel.

  This class has no supported interface - it exists to define the type of its
  instances and its instances exist to be passed to other functions.
  """

    def __init__(self, credentials):
        self._credentials = credentials


class CallCredentials(object):
    """A value encapsulating data asserting an identity over a channel.

  A CallCredentials may be composed with ChannelCredentials to always assert
  identity for every call over that Channel.

  This class has no supported interface - it exists to define the type of its
  instances and its instances exist to be passed to other functions.
  """

    def __init__(self, credentials):
        self._credentials = credentials


class AuthMetadataContext(six.with_metaclass(abc.ABCMeta)):
    """Provides information to call credentials metadata plugins.

  Attributes:
    service_url: A string URL of the service being called into.
    method_name: A string of the fully qualified method name being called.
  """


class AuthMetadataPluginCallback(six.with_metaclass(abc.ABCMeta)):
    """Callback object received by a metadata plugin."""

    def __call__(self, metadata, error):
        """Inform the gRPC runtime of the metadata to construct a CallCredentials.

    Args:
      metadata: The :term:`metadata` used to construct the CallCredentials.
      error: An Exception to indicate error or None to indicate success.
    """
        raise NotImplementedError()


class AuthMetadataPlugin(six.with_metaclass(abc.ABCMeta)):
    """A specification for custom authentication."""

    def __call__(self, context, callback):
        """Implements authentication by passing metadata to a callback.

    Implementations of this method must not block.

    Args:
      context: An AuthMetadataContext providing information on the RPC that the
        plugin is being called to authenticate.
      callback: An AuthMetadataPluginCallback to be invoked either synchronously
        or asynchronously.
    """
        raise NotImplementedError()


class ServerCredentials(object):
    """A value encapsulating the data required to open a secure port on a Server.

  This class has no supported interface - it exists to define the type of its
  instances and its instances exist to be passed to other functions.
  """

    def __init__(self, credentials):
        self._credentials = credentials


########################  Multi-Callable Interfaces  ###########################


class UnaryUnaryMultiCallable(six.with_metaclass(abc.ABCMeta)):
    """Affords invoking a unary-unary RPC."""

    @abc.abstractmethod
    def __call__(self, request, timeout=None, metadata=None, credentials=None):
        """Synchronously invokes the underlying RPC.

    Args:
      request: The request value for the RPC.
      timeout: An optional duration of time in seconds to allow for the RPC.
      metadata: Optional :term:`metadata` to be transmitted to the
        service-side of the RPC.
      credentials: An optional CallCredentials for the RPC.

    Returns:
      The response value for the RPC.

    Raises:
      RpcError: Indicating that the RPC terminated with non-OK status. The
        raised RpcError will also be a Call for the RPC affording the RPC's
        metadata, status code, and details.
    """
        raise NotImplementedError()

    @abc.abstractmethod
    def with_call(self, request, timeout=None, metadata=None, credentials=None):
        """Synchronously invokes the underlying RPC.

    Args:
      request: The request value for the RPC.
      timeout: An optional durating of time in seconds to allow for the RPC.
      metadata: Optional :term:`metadata` to be transmitted to the
        service-side of the RPC.
      credentials: An optional CallCredentials for the RPC.

    Returns:
      The response value for the RPC and a Call value for the RPC.

    Raises:
      RpcError: Indicating that the RPC terminated with non-OK status. The
        raised RpcError will also be a Call for the RPC affording the RPC's
        metadata, status code, and details.
    """
        raise NotImplementedError()

    @abc.abstractmethod
    def future(self, request, timeout=None, metadata=None, credentials=None):
        """Asynchronously invokes the underlying RPC.

    Args:
      request: The request value for the RPC.
      timeout: An optional duration of time in seconds to allow for the RPC.
      metadata: Optional :term:`metadata` to be transmitted to the
        service-side of the RPC.
      credentials: An optional CallCredentials for the RPC.

    Returns:
      An object that is both a Call for the RPC and a Future. In the event of
        RPC completion, the return Call-Future's result value will be the
        response message of the RPC. Should the event terminate with non-OK
        status, the returned Call-Future's exception value will be an RpcError.
    """
        raise NotImplementedError()


class UnaryStreamMultiCallable(six.with_metaclass(abc.ABCMeta)):
    """Affords invoking a unary-stream RPC."""

    @abc.abstractmethod
    def __call__(self, request, timeout=None, metadata=None, credentials=None):
        """Invokes the underlying RPC.

    Args:
      request: The request value for the RPC.
      timeout: An optional duration of time in seconds to allow for the RPC.
      metadata: An optional :term:`metadata` to be transmitted to the
        service-side of the RPC.
      credentials: An optional CallCredentials for the RPC.

    Returns:
      An object that is both a Call for the RPC and an iterator of response
        values. Drawing response values from the returned Call-iterator may
        raise RpcError indicating termination of the RPC with non-OK status.
    """
        raise NotImplementedError()


class StreamUnaryMultiCallable(six.with_metaclass(abc.ABCMeta)):
    """Affords invoking a stream-unary RPC in any call style."""

    @abc.abstractmethod
    def __call__(self,
                 request_iterator,
                 timeout=None,
                 metadata=None,
                 credentials=None):
        """Synchronously invokes the underlying RPC.

    Args:
      request_iterator: An iterator that yields request values for the RPC.
      timeout: An optional duration of time in seconds to allow for the RPC.
      metadata: Optional :term:`metadata` to be transmitted to the
        service-side of the RPC.
      credentials: An optional CallCredentials for the RPC.

    Returns:
      The response value for the RPC.

    Raises:
      RpcError: Indicating that the RPC terminated with non-OK status. The
        raised RpcError will also be a Call for the RPC affording the RPC's
        metadata, status code, and details.
    """
        raise NotImplementedError()

    @abc.abstractmethod
    def with_call(self,
                  request_iterator,
                  timeout=None,
                  metadata=None,
                  credentials=None):
        """Synchronously invokes the underlying RPC.

    Args:
      request_iterator: An iterator that yields request values for the RPC.
      timeout: An optional duration of time in seconds to allow for the RPC.
      metadata: Optional :term:`metadata` to be transmitted to the
        service-side of the RPC.
      credentials: An optional CallCredentials for the RPC.

    Returns:
      The response value for the RPC and a Call for the RPC.

    Raises:
      RpcError: Indicating that the RPC terminated with non-OK status. The
        raised RpcError will also be a Call for the RPC affording the RPC's
        metadata, status code, and details.
    """
        raise NotImplementedError()

    @abc.abstractmethod
    def future(self,
               request_iterator,
               timeout=None,
               metadata=None,
               credentials=None):
        """Asynchronously invokes the underlying RPC.

    Args:
      request_iterator: An iterator that yields request values for the RPC.
      timeout: An optional duration of time in seconds to allow for the RPC.
      metadata: Optional :term:`metadata` to be transmitted to the
        service-side of the RPC.
      credentials: An optional CallCredentials for the RPC.

    Returns:
      An object that is both a Call for the RPC and a Future. In the event of
        RPC completion, the return Call-Future's result value will be the
        response message of the RPC. Should the event terminate with non-OK
        status, the returned Call-Future's exception value will be an RpcError.
    """
        raise NotImplementedError()


class StreamStreamMultiCallable(six.with_metaclass(abc.ABCMeta)):
    """Affords invoking a stream-stream RPC in any call style."""

    @abc.abstractmethod
    def __call__(self,
                 request_iterator,
                 timeout=None,
                 metadata=None,
                 credentials=None):
        """Invokes the underlying RPC.

    Args:
      request_iterator: An iterator that yields request values for the RPC.
      timeout: An optional duration of time in seconds to allow for the RPC.
      metadata: Optional :term:`metadata` to be transmitted to the
        service-side of the RPC.
      credentials: An optional CallCredentials for the RPC.

    Returns:
      An object that is both a Call for the RPC and an iterator of response
        values. Drawing response values from the returned Call-iterator may
        raise RpcError indicating termination of the RPC with non-OK status.
    """
        raise NotImplementedError()


#############################  Channel Interface  ##############################


class Channel(six.with_metaclass(abc.ABCMeta)):
    """Affords RPC invocation via generic methods."""

    @abc.abstractmethod
    def subscribe(self, callback, try_to_connect=False):
        """Subscribes to this Channel's connectivity.

    Args:
      callback: A callable to be invoked and passed a ChannelConnectivity value
        describing this Channel's connectivity. The callable will be invoked
        immediately upon subscription and again for every change to this
        Channel's connectivity thereafter until it is unsubscribed or this
        Channel object goes out of scope.
      try_to_connect: A boolean indicating whether or not this Channel should
        attempt to connect if it is not already connected and ready to conduct
        RPCs.
    """
        raise NotImplementedError()

    @abc.abstractmethod
    def unsubscribe(self, callback):
        """Unsubscribes a callback from this Channel's connectivity.

    Args:
      callback: A callable previously registered with this Channel from having
        been passed to its "subscribe" method.
    """
        raise NotImplementedError()

    @abc.abstractmethod
    def unary_unary(self,
                    method,
                    request_serializer=None,
                    response_deserializer=None):
        """Creates a UnaryUnaryMultiCallable for a unary-unary method.

    Args:
      method: The name of the RPC method.
      request_serializer: Optional behaviour for serializing the request
        message. Request goes unserialized in case None is passed.
      response_deserializer: Optional behaviour for deserializing the response
        message. Response goes undeserialized in case None is passed.

    Returns:
      A UnaryUnaryMultiCallable value for the named unary-unary method.
    """
        raise NotImplementedError()

    @abc.abstractmethod
    def unary_stream(self,
                     method,
                     request_serializer=None,
                     response_deserializer=None):
        """Creates a UnaryStreamMultiCallable for a unary-stream method.

    Args:
      method: The name of the RPC method.
      request_serializer: Optional behaviour for serializing the request
        message. Request goes unserialized in case None is passed.
      response_deserializer: Optional behaviour for deserializing the response
        message. Response goes undeserialized in case None is passed.

    Returns:
      A UnaryStreamMultiCallable value for the name unary-stream method.
    """
        raise NotImplementedError()

    @abc.abstractmethod
    def stream_unary(self,
                     method,
                     request_serializer=None,
                     response_deserializer=None):
        """Creates a StreamUnaryMultiCallable for a stream-unary method.

    Args:
      method: The name of the RPC method.
      request_serializer: Optional behaviour for serializing the request
        message. Request goes unserialized in case None is passed.
      response_deserializer: Optional behaviour for deserializing the response
        message. Response goes undeserialized in case None is passed.

    Returns:
      A StreamUnaryMultiCallable value for the named stream-unary method.
    """
        raise NotImplementedError()

    @abc.abstractmethod
    def stream_stream(self,
                      method,
                      request_serializer=None,
                      response_deserializer=None):
        """Creates a StreamStreamMultiCallable for a stream-stream method.

    Args:
      method: The name of the RPC method.
      request_serializer: Optional behaviour for serializing the request
        message. Request goes unserialized in case None is passed.
      response_deserializer: Optional behaviour for deserializing the response
        message. Response goes undeserialized in case None is passed.

    Returns:
      A StreamStreamMultiCallable value for the named stream-stream method.
    """
        raise NotImplementedError()


##########################  Service-Side Context  ##############################


class ServicerContext(six.with_metaclass(abc.ABCMeta, RpcContext)):
    """A context object passed to method implementations."""

    @abc.abstractmethod
    def invocation_metadata(self):
        """Accesses the metadata from the invocation-side of the RPC.

    Returns:
      The invocation :term:`metadata`.
    """
        raise NotImplementedError()

    @abc.abstractmethod
    def peer(self):
        """Identifies the peer that invoked the RPC being serviced.

    Returns:
      A string identifying the peer that invoked the RPC being serviced.
    """
        raise NotImplementedError()

    @abc.abstractmethod
    def send_initial_metadata(self, initial_metadata):
        """Sends the initial metadata value to the invocation-side of the RPC.

    This method need not be called by method implementations if they have no
    service-side initial metadata to transmit.

    Args:
      initial_metadata: The initial :term:`metadata`.
    """
        raise NotImplementedError()

    @abc.abstractmethod
    def set_trailing_metadata(self, trailing_metadata):
        """Accepts the trailing metadata value of the RPC.

    This method need not be called by method implementations if they have no
    service-side trailing metadata to transmit.

    Args:
      trailing_metadata: The trailing :term:`metadata`.
    """
        raise NotImplementedError()

    @abc.abstractmethod
    def set_code(self, code):
        """Accepts the status code of the RPC.

    This method need not be called by method implementations if they wish the
    gRPC runtime to determine the status code of the RPC.

    Args:
      code: A StatusCode value to be transmitted to the invocation side of the
        RPC as the status code of the RPC.
    """
        raise NotImplementedError()

    @abc.abstractmethod
    def set_details(self, details):
        """Accepts the service-side details of the RPC.

    This method need not be called by method implementations if they have no
    details to transmit.

    Args:
      details: A string to be transmitted to the invocation side of the RPC as
        the status details of the RPC.
    """
        raise NotImplementedError()


#####################  Service-Side Handler Interfaces  ########################


class RpcMethodHandler(six.with_metaclass(abc.ABCMeta)):
    """An implementation of a single RPC method.

  Attributes:
    request_streaming: Whether the RPC supports exactly one request message or
      any arbitrary number of request messages.
    response_streaming: Whether the RPC supports exactly one response message or
      any arbitrary number of response messages.
    request_deserializer: A callable behavior that accepts a byte string and
      returns an object suitable to be passed to this object's business logic,
      or None to indicate that this object's business logic should be passed the
      raw request bytes.
    response_serializer: A callable behavior that accepts an object produced by
      this object's business logic and returns a byte string, or None to
      indicate that the byte strings produced by this object's business logic
      should be transmitted on the wire as they are.
    unary_unary: This object's application-specific business logic as a callable
      value that takes a request value and a ServicerContext object and returns
      a response value. Only non-None if both request_streaming and
      response_streaming are False.
    unary_stream: This object's application-specific business logic as a
      callable value that takes a request value and a ServicerContext object and
      returns an iterator of response values. Only non-None if request_streaming
      is False and response_streaming is True.
    stream_unary: This object's application-specific business logic as a
      callable value that takes an iterator of request values and a
      ServicerContext object and returns a response value. Only non-None if
      request_streaming is True and response_streaming is False.
    stream_stream: This object's application-specific business logic as a
      callable value that takes an iterator of request values and a
      ServicerContext object and returns an iterator of response values. Only
      non-None if request_streaming and response_streaming are both True.
  """


class HandlerCallDetails(six.with_metaclass(abc.ABCMeta)):
    """Describes an RPC that has just arrived for service.
  Attributes:
    method: The method name of the RPC.
    invocation_metadata: The :term:`metadata` from the invocation side of the RPC.
  """


class GenericRpcHandler(six.with_metaclass(abc.ABCMeta)):
    """An implementation of arbitrarily many RPC methods."""

    @abc.abstractmethod
    def service(self, handler_call_details):
        """Services an RPC (or not).

    Args:
      handler_call_details: A HandlerCallDetails describing the RPC.

    Returns:
      An RpcMethodHandler with which the RPC may be serviced, or None to
        indicate that this object will not be servicing the RPC.
    """
        raise NotImplementedError()


class ServiceRpcHandler(six.with_metaclass(abc.ABCMeta, GenericRpcHandler)):
    """An implementation of RPC methods belonging to a service.

  A service handles RPC methods with structured names of the form
  '/Service.Name/Service.MethodX', where 'Service.Name' is the value
  returned by service_name(), and 'Service.MethodX' is the service method
  name.  A service can have multiple service methods names, but only a single
  service name.
  """

    @abc.abstractmethod
    def service_name(self):
        """Returns this services name.

    Returns:
      The service name.
    """
        raise NotImplementedError()


#############################  Server Interface  ###############################


class Server(six.with_metaclass(abc.ABCMeta)):
    """Services RPCs."""

    @abc.abstractmethod
    def add_generic_rpc_handlers(self, generic_rpc_handlers):
        """Registers GenericRpcHandlers with this Server.

    This method is only safe to call before the server is started.

    Args:
      generic_rpc_handlers: An iterable of GenericRpcHandlers that will be used
        to service RPCs after this Server is started.
    """
        raise NotImplementedError()

    @abc.abstractmethod
    def add_insecure_port(self, address):
        """Reserves a port for insecure RPC service once this Server becomes active.

    This method may only be called before calling this Server's start method is
    called.

    Args:
      address: The address for which to open a port.

    Returns:
      An integer port on which RPCs will be serviced after this link has been
        started. This is typically the same number as the port number contained
        in the passed address, but will likely be different if the port number
        contained in the passed address was zero.
    """
        raise NotImplementedError()

    @abc.abstractmethod
    def add_secure_port(self, address, server_credentials):
        """Reserves a port for secure RPC service after this Server becomes active.

    This method may only be called before calling this Server's start method is
    called.

    Args:
      address: The address for which to open a port.
      server_credentials: A ServerCredentials.

    Returns:
      An integer port on which RPCs will be serviced after this link has been
        started. This is typically the same number as the port number contained
        in the passed address, but will likely be different if the port number
        contained in the passed address was zero.
    """
        raise NotImplementedError()

    @abc.abstractmethod
    def start(self):
        """Starts this Server's service of RPCs.

    This method may only be called while the server is not serving RPCs (i.e. it
    is not idempotent).
    """
        raise NotImplementedError()

    @abc.abstractmethod
    def stop(self, grace):
        """Stops this Server's service of RPCs.

    All calls to this method immediately stop service of new RPCs. When existing
    RPCs are aborted is controlled by the grace period parameter passed to this
    method.

    This method may be called at any time and is idempotent. Passing a smaller
    grace value than has been passed in a previous call will have the effect of
    stopping the Server sooner. Passing a larger grace value than has been
    passed in a previous call will not have the effect of stopping the server
    later.

    This method does not block for any significant length of time. If None is
    passed as the grace value, existing RPCs are immediately aborted and this
    method blocks until this Server is completely stopped.

    Args:
      grace: A duration of time in seconds or None. If a duration of time in
        seconds, the time to allow existing RPCs to complete before being
        aborted by this Server's stopping. If None, all RPCs will be aborted
        immediately and this method will block until this Server is completely
        stopped.

    Returns:
      A threading.Event that will be set when this Server has completely
      stopped. The returned event may not be set until after the full grace
      period (if some ongoing RPC continues for the full length of the period)
      of it may be set much sooner (such as if this Server had no RPCs underway
      at the time it was stopped or if all RPCs that it had underway completed
      very early in the grace period).
    """
        raise NotImplementedError()


#################################  Functions    ################################


def unary_unary_rpc_method_handler(behavior,
                                   request_deserializer=None,
                                   response_serializer=None):
    """Creates an RpcMethodHandler for a unary-unary RPC method.

  Args:
    behavior: The implementation of an RPC method as a callable behavior taking
      a single request value and returning a single response value.
    request_deserializer: An optional request deserialization behavior.
    response_serializer: An optional response serialization behavior.

  Returns:
    An RpcMethodHandler for a unary-unary RPC method constructed from the given
      parameters.
  """
    from grpc import _utilities
    return _utilities.RpcMethodHandler(False, False, request_deserializer,
                                       response_serializer, behavior, None,
                                       None, None)


def unary_stream_rpc_method_handler(behavior,
                                    request_deserializer=None,
                                    response_serializer=None):
    """Creates an RpcMethodHandler for a unary-stream RPC method.

  Args:
    behavior: The implementation of an RPC method as a callable behavior taking
      a single request value and returning an iterator of response values.
    request_deserializer: An optional request deserialization behavior.
    response_serializer: An optional response serialization behavior.

  Returns:
    An RpcMethodHandler for a unary-stream RPC method constructed from the
      given parameters.
  """
    from grpc import _utilities
    return _utilities.RpcMethodHandler(False, True, request_deserializer,
                                       response_serializer, None, behavior,
                                       None, None)


def stream_unary_rpc_method_handler(behavior,
                                    request_deserializer=None,
                                    response_serializer=None):
    """Creates an RpcMethodHandler for a stream-unary RPC method.

  Args:
    behavior: The implementation of an RPC method as a callable behavior taking
      an iterator of request values and returning a single response value.
    request_deserializer: An optional request deserialization behavior.
    response_serializer: An optional response serialization behavior.

  Returns:
    An RpcMethodHandler for a stream-unary RPC method constructed from the
      given parameters.
  """
    from grpc import _utilities
    return _utilities.RpcMethodHandler(True, False, request_deserializer,
                                       response_serializer, None, None,
                                       behavior, None)


def stream_stream_rpc_method_handler(behavior,
                                     request_deserializer=None,
                                     response_serializer=None):
    """Creates an RpcMethodHandler for a stream-stream RPC method.

  Args:
    behavior: The implementation of an RPC method as a callable behavior taking
      an iterator of request values and returning an iterator of response
      values.
    request_deserializer: An optional request deserialization behavior.
    response_serializer: An optional response serialization behavior.

  Returns:
    An RpcMethodHandler for a stream-stream RPC method constructed from the
      given parameters.
  """
    from grpc import _utilities
    return _utilities.RpcMethodHandler(True, True, request_deserializer,
                                       response_serializer, None, None, None,
                                       behavior)


def method_handlers_generic_handler(service, method_handlers):
    """Creates a grpc.GenericRpcHandler from RpcMethodHandlers.

  Args:
    service: A service name to be used for the given method handlers.
    method_handlers: A dictionary from method name to RpcMethodHandler
      implementing the named method.

  Returns:
    A GenericRpcHandler constructed from the given parameters.
  """
    from grpc import _utilities
    return _utilities.DictionaryGenericHandler(service, method_handlers)


def ssl_channel_credentials(root_certificates=None,
                            private_key=None,
                            certificate_chain=None):
    """Creates a ChannelCredentials for use with an SSL-enabled Channel.

  Args:
    root_certificates: The PEM-encoded root certificates or unset to ask for
      them to be retrieved from a default location.
    private_key: The PEM-encoded private key to use or unset if no private key
      should be used.
    certificate_chain: The PEM-encoded certificate chain to use or unset if no
      certificate chain should be used.

  Returns:
    A ChannelCredentials for use with an SSL-enabled Channel.
  """
    if private_key is not None or certificate_chain is not None:
        pair = _cygrpc.SslPemKeyCertPair(private_key, certificate_chain)
    else:
        pair = None
    return ChannelCredentials(
        _cygrpc.channel_credentials_ssl(root_certificates, pair))


def metadata_call_credentials(metadata_plugin, name=None):
    """Construct CallCredentials from an AuthMetadataPlugin.

  Args:
    metadata_plugin: An AuthMetadataPlugin to use as the authentication behavior
      in the created CallCredentials.
    name: A name for the plugin.

  Returns:
    A CallCredentials.
  """
    from grpc import _plugin_wrapping
    if name is None:
        try:
            effective_name = metadata_plugin.__name__
        except AttributeError:
            effective_name = metadata_plugin.__class__.__name__
    else:
        effective_name = name
    return CallCredentials(
        _plugin_wrapping.call_credentials_metadata_plugin(metadata_plugin,
                                                          effective_name))


def access_token_call_credentials(access_token):
    """Construct CallCredentials from an access token.

  Args:
    access_token: A string to place directly in the http request
      authorization header, ie "authorization: Bearer <access_token>".

  Returns:
    A CallCredentials.
  """
    from grpc import _auth
    return metadata_call_credentials(
        _auth.AccessTokenCallCredentials(access_token))


def composite_call_credentials(*call_credentials):
    """Compose multiple CallCredentials to make a new CallCredentials.

  Args:
    *call_credentials: At least two CallCredentials objects.

  Returns:
    A CallCredentials object composed of the given CallCredentials objects.
  """
    from grpc import _credential_composition
    cygrpc_call_credentials = tuple(
        single_call_credentials._credentials
        for single_call_credentials in call_credentials)
    return CallCredentials(
        _credential_composition.call(cygrpc_call_credentials))


def composite_channel_credentials(channel_credentials, *call_credentials):
    """Compose a ChannelCredentials and one or more CallCredentials objects.

  Args:
    channel_credentials: A ChannelCredentials.
    *call_credentials: One or more CallCredentials objects.

  Returns:
    A ChannelCredentials composed of the given ChannelCredentials and
      CallCredentials objects.
  """
    from grpc import _credential_composition
    cygrpc_call_credentials = tuple(
        single_call_credentials._credentials
        for single_call_credentials in call_credentials)
    return ChannelCredentials(
        _credential_composition.channel(channel_credentials._credentials,
                                        cygrpc_call_credentials))


def ssl_server_credentials(private_key_certificate_chain_pairs,
                           root_certificates=None,
                           require_client_auth=False):
    """Creates a ServerCredentials for use with an SSL-enabled Server.

  Args:
    private_key_certificate_chain_pairs: A nonempty sequence each element of
      which is a pair the first element of which is a PEM-encoded private key
      and the second element of which is the corresponding PEM-encoded
      certificate chain.
    root_certificates: PEM-encoded client root certificates to be used for
      verifying authenticated clients. If omitted, require_client_auth must also
      be omitted or be False.
    require_client_auth: A boolean indicating whether or not to require clients
      to be authenticated. May only be True if root_certificates is not None.

  Returns:
    A ServerCredentials for use with an SSL-enabled Server.
  """
    if len(private_key_certificate_chain_pairs) == 0:
        raise ValueError(
            'At least one private key-certificate chain pair is required!')
    elif require_client_auth and root_certificates is None:
        raise ValueError(
            'Illegal to require client auth without providing root certificates!'
        )
    else:
        return ServerCredentials(
            _cygrpc.server_credentials_ssl(root_certificates, [
                _cygrpc.SslPemKeyCertPair(key, pem)
                for key, pem in private_key_certificate_chain_pairs
            ], require_client_auth))


def channel_ready_future(channel):
    """Creates a Future tracking when a Channel is ready.

  Cancelling the returned Future does not tell the given Channel to abandon
  attempts it may have been making to connect; cancelling merely deactivates the
  returned Future's subscription to the given Channel's connectivity.

  Args:
    channel: A Channel.

  Returns:
    A Future that matures when the given Channel has connectivity
      ChannelConnectivity.READY.
  """
    from grpc import _utilities
    return _utilities.channel_ready_future(channel)


def insecure_channel(target, options=None):
    """Creates an insecure Channel to a server.

  Args:
    target: The target to which to connect.
    options: A sequence of string-value pairs according to which to configure
      the created channel.

  Returns:
    A Channel to the target through which RPCs may be conducted.
  """
    from grpc import _channel
    return _channel.Channel(target, () if options is None else options, None)


def secure_channel(target, credentials, options=None):
    """Creates a secure Channel to a server.

  Args:
    target: The target to which to connect.
    credentials: A ChannelCredentials instance.
    options: A sequence of string-value pairs according to which to configure
      the created channel.

  Returns:
    A Channel to the target through which RPCs may be conducted.
  """
    from grpc import _channel
    return _channel.Channel(target, () if options is None else options,
                            credentials._credentials)


def server(thread_pool, handlers=None, options=None):
    """Creates a Server with which RPCs can be serviced.

  Args:
    thread_pool: A futures.ThreadPoolExecutor to be used by the returned Server
      to service RPCs.
    handlers: An optional sequence of GenericRpcHandlers to be used to service
      RPCs after the returned Server is started. These handlers need not be the
      only handlers the server will use to service RPCs; other handlers may
      later be added by calling add_generic_rpc_handlers any time before the
      returned Server is started.
    options: A sequence of string-value pairs according to which to configure
      the created server.

  Returns:
    A Server with which RPCs can be serviced.
  """
    from grpc import _server
    return _server.Server(thread_pool, () if handlers is None else handlers, ()
                          if options is None else options)


###################################  __all__  #################################

__all__ = (
    'FutureTimeoutError',
    'FutureCancelledError',
    'Future',
    'ChannelConnectivity',
    'StatusCode',
    'RpcError',
    'RpcContext',
    'Call',
    'ChannelCredentials',
    'CallCredentials',
    'AuthMetadataContext',
    'AuthMetadataPluginCallback',
    'AuthMetadataPlugin',
    'ServerCredentials',
    'UnaryUnaryMultiCallable',
    'UnaryStreamMultiCallable',
    'StreamUnaryMultiCallable',
    'StreamStreamMultiCallable',
    'Channel',
    'ServicerContext',
    'RpcMethodHandler',
    'HandlerCallDetails',
    'GenericRpcHandler',
    'ServiceRpcHandler',
    'Server',
    'unary_unary_rpc_method_handler',
    'unary_stream_rpc_method_handler',
    'stream_unary_rpc_method_handler',
    'stream_stream_rpc_method_handler',
    'method_handlers_generic_handler',
    'ssl_channel_credentials',
    'metadata_call_credentials',
    'access_token_call_credentials',
    'composite_call_credentials',
    'composite_channel_credentials',
    'ssl_server_credentials',
    'channel_ready_future',
    'insecure_channel',
    'secure_channel',
    'server',)

############################### Extension Shims ################################

# Here to maintain backwards compatibility; avoid using these in new code!
try:
    import grpc_tools
    sys.modules.update({'grpc.tools': grpc_tools})
except ImportError:
    pass
try:
    import grpc_health
    sys.modules.update({'grpc.health': grpc_health})
except ImportError:
    pass
try:
    import grpc_reflection
    sys.modules.update({'grpc.reflection': grpc_reflection})
except ImportError:
    pass
