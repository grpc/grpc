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

__import__('pkg_resources').declare_namespace(__name__)

import abc
import enum

import six

from grpc._cython import cygrpc as _cygrpc


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
  IDLE              = (_cygrpc.ConnectivityState.idle, 'idle')
  CONNECTING        = (_cygrpc.ConnectivityState.connecting, 'connecting')
  READY             = (_cygrpc.ConnectivityState.ready, 'ready')
  TRANSIENT_FAILURE = (
      _cygrpc.ConnectivityState.transient_failure, 'transient failure')
  SHUTDOWN          = (_cygrpc.ConnectivityState.shutdown, 'shutdown')


@enum.unique
class StatusCode(enum.Enum):
  """Mirrors grpc_status_code in the gRPC Core."""
  OK                  = (_cygrpc.StatusCode.ok, 'ok')
  CANCELLED           = (_cygrpc.StatusCode.cancelled, 'cancelled')
  UNKNOWN             = (_cygrpc.StatusCode.unknown, 'unknown')
  INVALID_ARGUMENT    = (
      _cygrpc.StatusCode.invalid_argument, 'invalid argument')
  DEADLINE_EXCEEDED   = (
      _cygrpc.StatusCode.deadline_exceeded, 'deadline exceeded')
  NOT_FOUND           = (_cygrpc.StatusCode.not_found, 'not found')
  ALREADY_EXISTS      = (_cygrpc.StatusCode.already_exists, 'already exists')
  PERMISSION_DENIED   = (
      _cygrpc.StatusCode.permission_denied, 'permission denied')
  RESOURCE_EXHAUSTED  = (
      _cygrpc.StatusCode.resource_exhausted, 'resource exhausted')
  FAILED_PRECONDITION = (
      _cygrpc.StatusCode.failed_precondition, 'failed precondition')
  ABORTED             = (_cygrpc.StatusCode.aborted, 'aborted')
  OUT_OF_RANGE        = (_cygrpc.StatusCode.out_of_range, 'out of range')
  UNIMPLEMENTED       = (_cygrpc.StatusCode.unimplemented, 'unimplemented')
  INTERNAL            = (_cygrpc.StatusCode.internal, 'internal')
  UNAVAILABLE         = (_cygrpc.StatusCode.unavailable, 'unavailable')
  DATA_LOSS           = (_cygrpc.StatusCode.data_loss, 'data loss')
  UNAUTHENTICATED     = (_cygrpc.StatusCode.unauthenticated, 'unauthenticated')


#############################  gRPC Exceptions  ################################


class RpcError(Exception):
  """Indicates an RPC terminated with a non-OK-status.

  Attributes:
    code: The StatusCode of the terminated RPC
    details: The details of the terminated RPC
  """

  def __init__(self, code, details):
    super(RpcError, self).__init__(
        'RPC Failure (code: {}, details: {})'.format(code, details))
    self.code = code
    self.details = details


class RpcTimeoutError(Exception):
  """Indicates that a timeout occured waiting for an RPC result."""
  pass

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
  def add_done_callback(self, callback):
    """Registers a callback to be called on RPC termination.

    This callback will be invoked immediately if this RPC has already
    terminated.  Any Exception raised by callback will be logged.

    Args:
      callback: A callable to be invoked with a single argument, this context.
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
      The initial metadata as a sequence of pairs of bytes.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def trailing_metadata(self):
    """Accesses the trailing metadata from the service-side of the RPC.

    This method blocks until the value is available.

    Returns:
      The trailing metadata as a sequence of pairs of bytes.
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
      The bytes of the details of the RPC.
    """
    raise NotImplementedError()


class UnaryCall(six.with_metaclass(abc.ABCMeta, Call)):
  """Invocation-side utility object for a unary response RPC."""

  @abc.abstractmethod
  def response(self, timeout=None):
    """Access the response of the RPC.

    This method blocks until the value is available.

    Args:
      timeout: The maximum time in seconds to wait for
        a response value, or None to specify no timeout.

    Returns:
      The response of the RPC.

    Raises:
      RpcTimeoutError: No response was available within `timeout`
      RpcError: Indicating that the RPC terminated with non-OK status.
    """
    raise NotImplementedError()


class StreamingCall(six.with_metaclass(abc.ABCMeta, Call)):
  """Invocation-side utility object for a Streaming response RPC."""

  @abc.abstractmethod
  def response_iterator(self, timeout=None):
    """Access the streamed responses of the RPC.

    This method blocks until the initial metadata is available or
    the call completes.  Subsequent calls to next() will block until
    the next response is available.

    Args:
      timeout: The maximum time in seconds to wait for
        a response value, or None to specify no timeout.

    Returns:
      An iterator over the responses of the RPC.

    Raises:
      RpcTimeoutError: No initial metadata was available within `timeout`.
      RpcError: Indicating that the RPC terminated with non-OK status.
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
      metadata: An iterable of 2-sequences (e.g. tuples) of metadata key/value
        pairs.
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
  def __call__(
      self, request, timeout=None, metadata=None, credentials=None):
    """Invokes the underlying RPC.

    This method blocks until the RPC has completed.

    Args:
      request: The request value for the RPC.
      timeout: An optional duration of time in seconds to allow for the RPC.
      metadata: An optional sequence of pairs of bytes to be transmitted to the
        service-side of the RPC.
      credentials: An optional CallCredentials for the RPC.

    Returns:
      The response of the RPC.

    Raises:
      RpcError: Indicating that the RPC terminated with non-OK status.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def call(
      self, request, timeout=None, metadata=None, credentials=None):
    """Invokes the underlying RPC.

    This method blocks until the RPC has completed.

    Args:
      request: The request value for the RPC.
      timeout: An optional duration of time in seconds to allow for the RPC.
      metadata: An optional sequence of pairs of bytes to be transmitted to the
        service-side of the RPC.
      credentials: An optional CallCredentials for the RPC.

    Returns:
      A completed UnaryCall for the RPC.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def call_async(self, request, timeout=None, metadata=None, credentials=None):
    """Asynchrnously invokes the underlying RPC.

    Args:
      request: The request value for the RPC.
      timeout: An optional duration of time in seconds to allow for the RPC.
      metadata: An optional sequence of pairs of bytes to be transmitted to the
        service-side of the RPC.
      credentials: An optional CallCredentials for the RPC.

    Returns:
      An in-flight UnaryCall for the RPC.
    """
    raise NotImplementedError()


class UnaryStreamMultiCallable(six.with_metaclass(abc.ABCMeta)):
  """Affords invoking a unary-stream RPC."""

  @abc.abstractmethod
  def __call__(self, request, timeout=None, metadata=None, credentials=None):
    """Invokes the underlying RPC.

    This method blocks until initial metadata is available or the call has
    terminated.

    Args:
      request: The request value for the RPC.
      timeout: An optional duration of time in seconds to allow for the RPC.
      metadata: An optional sequence of pairs of bytes to be transmitted to the
        service-side of the RPC.
      credentials: An optional CallCredentials for the RPC.

    Returns:
      An iterator of the responses for the RPC.

    Raises:
      RpcError: Indicating that the RPC terminated with non-OK status.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def call(self, request, timeout=None, metadata=None, credentials=None):
    """Invokes the underlying RPC.

    This method blocks until the initial metadata is available.

    Args:
      request: The request value for the RPC.
      timeout: An optional duration of time in seconds to allow for the RPC.
      metadata: An optional sequence of pairs of bytes to be transmitted to the
        service-side of the RPC.
      credentials: An optional CallCredentials for the RPC.

    Returns:
      An in-flight StreamingCall for the RPC.
    """
    raise NotImplementedError()

  def call_async(self, request, timeout=None, metadata=None, credentials=None):
    """Asynchronously invokes the underlying RPC.

    Args:
      request: The request value for the RPC.
      timeout: An optional duration of time in seconds to allow for the RPC.
      metadata: An optional sequence of pairs of bytes to be transmitted to the
        service-side of the RPC.
      credentials: An optional CallCredentials for the RPC.

    Returns:
      An in-flight StreamingCall for the RPC.
    """
    raise NotImplementedError()


class StreamUnaryMultiCallable(six.with_metaclass(abc.ABCMeta)):
  """Affords invoking a stream-unary RPC in any call style."""

  @abc.abstractmethod
  def __call__(
      self, request_iterator, timeout=None, metadata=None, credentials=None):
    """Synchronously invokes the underlying RPC.

    This method blocks until the RPC response is available.

    Args:
      request_iterator: An iterator that yields request values for the RPC.
      timeout: An optional duration of time in seconds to allow for the RPC.
      metadata: An optional sequence of pairs of bytes to be transmitted to the
        service-side of the RPC.
      credentials: An optional CallCredentials for the RPC.

    Returns:
      A the response of the RPC.

    Raises:
      RpcError: Indicating that the RPC terminated with non-OK status.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def call(
      self, request_iterator, timeout=None, metadata=None, credentials=None):
    """Synchronously invokes the underlying RPC.

    Args:
      request_iterator: An iterator that yields request values for the RPC.
      timeout: An optional duration of time in seconds to allow for the RPC.
      metadata: An optional sequence of pairs of bytes to be transmitted to the
        service-side of the RPC.
      credentials: An optional CallCredentials for the RPC.

    Returns:
      A completed UnaryCall for the RPC.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def call_async(
      self, request_iterator, timeout=None, metadata=None, credentials=None):
    """Asynchronously invokes the underlying RPC.

    Args:
      request_iterator: An iterator that yields request values for the RPC.
      timeout: An optional duration of time in seconds to allow for the RPC.
      metadata: An optional sequence of pairs of bytes to be transmitted to the
        service-side of the RPC.
      credentials: An optional CallCredentials for the RPC.

    Returns:
      An in-flight UnaryCall for the RPC.
    """
    raise NotImplementedError()


class StreamStreamMultiCallable(six.with_metaclass(abc.ABCMeta)):
  """Affords invoking a stream-stream RPC in any call style."""

  @abc.abstractmethod
  def __call__(
      self, request_iterator, timeout=None, metadata=None, credentials=None):
    """Invokes the underlying RPC.

    This method blocks until initial metadata is available or the call has
    terminated.

    Args:
      request_iterator: An iterator that yields request values for the RPC.
      timeout: An optional duration of time in seconds to allow for the RPC.
      metadata: An optional sequence of pairs of bytes to be transmitted to the
        service-side of the RPC.
      credentials: An optional CallCredentials for the RPC.

    Returns:
      A iterator over responses for the RPC.

    Raises:
      RpcError: Indicating that the RPC terminated with non-OK status.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def call(
      self, request_iterator, timeout=None, metadata=None, credentials=None):
    """Invokes the underlying RPC.

    Args:
      request_iterator: An iterator that yields request values for the RPC.
      timeout: An optional duration of time in seconds to allow for the RPC.
      metadata: An optional sequence of pairs of bytes to be transmitted to the
        service-side of the RPC.
      credentials: An optional CallCredentials for the RPC.

    Returns:
      An in-flight StreamingCall for the RPC.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def call_async(
      self, request_iterator, timeout=None, metadata=None, credentials=None):
    """Asynchronously invokes the underlying RPC.

    Args:
      request_iterator: An iterator that yields request values for the RPC.
      timeout: An optional duration of time in seconds to allow for the RPC.
      metadata: An optional sequence of pairs of bytes to be transmitted to the
        service-side of the RPC.
      credentials: An optional CallCredentials for the RPC.

    Returns:
      An in-flight UnaryCall for the RPC.
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
  def wait_for_ready(self, timeout=None):
    """Blocks until a channel is in state ChannelConnectivity.READY.

    This channel will attempt to establish a connection if it is not connected.

    Args:
      timeout: The maximum time in seconds to wait for this channel to be ready,
        or None to wait indefinately.

    Raises:
      RpcTimeoutError:  the channel was not ready when `timeout` occurred
    """

  @abc.abstractmethod
  def unary_unary(
      self, method, request_serializer=None, response_deserializer=None):
    """Creates a UnaryUnaryMultiCallable for a unary-unary method.

    Args:
      method: The name of the RPC method.

    Returns:
      A UnaryUnaryMultiCallable value for the named unary-unary method.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def unary_stream(
      self, method, request_serializer=None, response_deserializer=None):
    """Creates a UnaryStreamMultiCallable for a unary-stream method.

    Args:
      method: The name of the RPC method.

    Returns:
      A UnaryStreamMultiCallable value for the name unary-stream method.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def stream_unary(
      self, method, request_serializer=None, response_deserializer=None):
    """Creates a StreamUnaryMultiCallable for a stream-unary method.

    Args:
      method: The name of the RPC method.

    Returns:
      A StreamUnaryMultiCallable value for the named stream-unary method.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def stream_stream(
      self, method, request_serializer=None, response_deserializer=None):
    """Creates a StreamStreamMultiCallable for a stream-stream method.

    Args:
      method: The name of the RPC method.

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
      The invocation metadata object as a sequence of pairs of bytes.
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
      initial_metadata: The initial metadata of the RPC as a sequence of pairs
        of bytes.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def set_trailing_metadata(self, trailing_metadata):
    """Accepts the trailing metadata value of the RPC.

    This method need not be called by method implementations if they have no
    service-side trailing metadata to transmit.

    Args:
      trailing_metadata: The trailing metadata of the RPC as a sequence of pairs
        of bytes.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def set_code(self, code):
    """Accepts the status code of the RPC.

    This method need not be called by method implementations if they wish the
    gRPC runtime to determine the status code of the RPC.

    Args:
      code: The integer status code of the RPC to be transmitted to the
        invocation side of the RPC.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def set_details(self, details):
    """Accepts the service-side details of the RPC.

    This method need not be called by method implementations if they have no
    details to transmit.

    Args:
      details: The details bytes of the RPC to be transmitted to
        the invocation side of the RPC.
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
    invocation_metadata: The metadata from the invocation side of the RPC.
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

    Args:
      grace: A duration of time in seconds to allow existing RPCs to complete
        before being aborted by this Server's stopping. If None, this method
        will block until the server is completely stopped.

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


def unary_unary_rpc_method_handler(
    behavior, request_deserializer=None, response_serializer=None):
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
  return _utilities.RpcMethodHandler(
      False, False, request_deserializer, response_serializer, behavior, None,
      None, None)


def unary_stream_rpc_method_handler(
    behavior, request_deserializer=None, response_serializer=None):
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
  return _utilities.RpcMethodHandler(
      False, True, request_deserializer, response_serializer, None, behavior,
      None, None)


def stream_unary_rpc_method_handler(
    behavior, request_deserializer=None, response_serializer=None):
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
  return _utilities.RpcMethodHandler(
      True, False, request_deserializer, response_serializer, None, None,
      behavior, None)


def stream_stream_rpc_method_handler(
        behavior, request_deserializer=None, response_serializer=None):
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
  return _utilities.RpcMethodHandler(
      True, True, request_deserializer, response_serializer, None, None, None,
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


def ssl_channel_credentials(
    root_certificates=None, private_key=None, certificate_chain=None):
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
      _plugin_wrapping.call_credentials_metadata_plugin(
          metadata_plugin, effective_name))


def access_token_call_credentials(access_token):
  """Construct CallCredentials from an access token.

  Args:
    access_token: A string to place directly in the http request
      authorization header, ie "Authorization: Bearer <access_token>".

  Returns:
    A CallCredentials.
  """
  from grpc import _auth
  return metadata_call_credentials(
      _auth.AccessTokenCallCredentials(access_token))


def composite_call_credentials(call_credentials, additional_call_credentials):
  """Compose two CallCredentials to make a new one.

  Args:
    call_credentials: A CallCredentials object.
    additional_call_credentials: Another CallCredentials object to compose on
      top of call_credentials.

  Returns:
    A new CallCredentials composed of the two given CallCredentials.
  """
  return CallCredentials(
      _cygrpc.call_credentials_composite(
          call_credentials._credentials,
          additional_call_credentials._credentials))


def composite_channel_credentials(channel_credentials, call_credentials):
  """Compose a ChannelCredentials and a CallCredentials.

  Args:
    channel_credentials: A ChannelCredentials.
    call_credentials: A CallCredentials.

  Returns:
    A ChannelCredentials composed of the given ChannelCredentials and
      CallCredentials.
  """
  return ChannelCredentials(
      _cygrpc.channel_credentials_composite(
          channel_credentials._credentials, call_credentials._credentials))


def ssl_server_credentials(
    private_key_certificate_chain_pairs, root_certificates=None,
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
        'Illegal to require client auth without providing root certificates!')
  else:
    return ServerCredentials(
        _cygrpc.server_credentials_ssl(
        root_certificates,
        [_cygrpc.SslPemKeyCertPair(key, pem)
         for key, pem in private_key_certificate_chain_pairs],
        require_client_auth))


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
  return _channel.Channel(target, options, None)


def secure_channel(target, credentials, options=None):
  """Creates an insecure Channel to a server.

  Args:
    target: The target to which to connect.
    credentials: A ChannelCredentials instance.
    options: A sequence of string-value pairs according to which to configure
      the created channel.

  Returns:
    A Channel to the target through which RPCs may be conducted.
  """
  from grpc import _channel
  return _channel.Channel(target, options, credentials)


def server(generic_rpc_handlers, thread_pool, options=None):
  """Creates a Server with which RPCs can be serviced.

  The GenericRpcHandlers passed to this function needn't be the only
  GenericRpcHandlers that will be used to serve RPCs; others may be added later
  by calling add_generic_rpc_handlers any time before the returned server is
  started.

  Args:
    generic_rpc_handlers: Some number of GenericRpcHandlers that will be used
      to service RPCs after the returned Server is started.
    thread_pool: A futures.ThreadPoolExecutor to be used by the returned Server
      to service RPCs.

  Returns:
    A Server with which RPCs can be serviced.
  """
  from grpc import _server
  return _server.Server(generic_rpc_handlers, thread_pool)


###################################  __all__  #################################


__all__ = (
    'ChannelConnectivity',
    'StatusCode',
    'RpcError',
    'RpcTimeoutError',
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
    'insecure_channel',
    'secure_channel',
    'server',
)
