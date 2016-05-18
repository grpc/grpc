# Copyright 2015, Google Inc.
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

import abc
import collections
import enum

import six

from grpc._cython import cygrpc


class GrpcChannelArgumentKeys(enum.Enum):
  """Mirrors keys used in grpc_channel_args for GRPC-specific arguments."""
  SSL_TARGET_NAME_OVERRIDE = 'grpc.ssl_target_name_override'


@enum.unique
class CallError(enum.IntEnum):
  """Mirrors grpc_call_error in the C core."""
  OK                        = cygrpc.CallError.ok
  ERROR                     = cygrpc.CallError.error
  ERROR_NOT_ON_SERVER       = cygrpc.CallError.not_on_server
  ERROR_NOT_ON_CLIENT       = cygrpc.CallError.not_on_client
  ERROR_ALREADY_ACCEPTED    = cygrpc.CallError.already_accepted
  ERROR_ALREADY_INVOKED     = cygrpc.CallError.already_invoked
  ERROR_NOT_INVOKED         = cygrpc.CallError.not_invoked
  ERROR_ALREADY_FINISHED    = cygrpc.CallError.already_finished
  ERROR_TOO_MANY_OPERATIONS = cygrpc.CallError.too_many_operations
  ERROR_INVALID_FLAGS       = cygrpc.CallError.invalid_flags
  ERROR_INVALID_METADATA    = cygrpc.CallError.invalid_metadata


@enum.unique
class StatusCode(enum.IntEnum):
  """Mirrors grpc_status_code in the C core."""
  OK                  = cygrpc.StatusCode.ok
  CANCELLED           = cygrpc.StatusCode.cancelled
  UNKNOWN             = cygrpc.StatusCode.unknown
  INVALID_ARGUMENT    = cygrpc.StatusCode.invalid_argument
  DEADLINE_EXCEEDED   = cygrpc.StatusCode.deadline_exceeded
  NOT_FOUND           = cygrpc.StatusCode.not_found
  ALREADY_EXISTS      = cygrpc.StatusCode.already_exists
  PERMISSION_DENIED   = cygrpc.StatusCode.permission_denied
  RESOURCE_EXHAUSTED  = cygrpc.StatusCode.resource_exhausted
  FAILED_PRECONDITION = cygrpc.StatusCode.failed_precondition
  ABORTED             = cygrpc.StatusCode.aborted
  OUT_OF_RANGE        = cygrpc.StatusCode.out_of_range
  UNIMPLEMENTED       = cygrpc.StatusCode.unimplemented
  INTERNAL            = cygrpc.StatusCode.internal
  UNAVAILABLE         = cygrpc.StatusCode.unavailable
  DATA_LOSS           = cygrpc.StatusCode.data_loss
  UNAUTHENTICATED     = cygrpc.StatusCode.unauthenticated


@enum.unique
class OpWriteFlags(enum.IntEnum):
  """Mirrors defined write-flag constants in the C core."""
  WRITE_BUFFER_HINT = cygrpc.WriteFlag.buffer_hint
  WRITE_NO_COMPRESS = cygrpc.WriteFlag.no_compress


@enum.unique
class OpType(enum.IntEnum):
  """Mirrors grpc_op_type in the C core."""
  SEND_INITIAL_METADATA   = cygrpc.OperationType.send_initial_metadata
  SEND_MESSAGE            = cygrpc.OperationType.send_message
  SEND_CLOSE_FROM_CLIENT  = cygrpc.OperationType.send_close_from_client
  SEND_STATUS_FROM_SERVER = cygrpc.OperationType.send_status_from_server
  RECV_INITIAL_METADATA   = cygrpc.OperationType.receive_initial_metadata
  RECV_MESSAGE            = cygrpc.OperationType.receive_message
  RECV_STATUS_ON_CLIENT   = cygrpc.OperationType.receive_status_on_client
  RECV_CLOSE_ON_SERVER    = cygrpc.OperationType.receive_close_on_server


@enum.unique
class EventType(enum.IntEnum):
  """Mirrors grpc_completion_type in the C core."""
  QUEUE_SHUTDOWN = cygrpc.CompletionType.queue_shutdown
  QUEUE_TIMEOUT  = cygrpc.CompletionType.queue_timeout
  OP_COMPLETE    = cygrpc.CompletionType.operation_complete


@enum.unique
class ConnectivityState(enum.IntEnum):
  """Mirrors grpc_connectivity_state in the C core."""
  IDLE              = cygrpc.ConnectivityState.idle
  CONNECTING        = cygrpc.ConnectivityState.connecting
  READY             = cygrpc.ConnectivityState.ready
  TRANSIENT_FAILURE = cygrpc.ConnectivityState.transient_failure
  FATAL_FAILURE     = cygrpc.ConnectivityState.fatal_failure


class Status(collections.namedtuple(
    'Status', [
        'code',
        'details',
    ])):
  """The end status of a GRPC call.

  Attributes:
    code (StatusCode): ...
    details (str): ...
  """


class CallDetails(collections.namedtuple(
    'CallDetails', [
        'method',
        'host',
        'deadline',
    ])):
  """Provides information to the server about the client's call.

  Attributes:
    method (str): ...
    host (str): ...
    deadline (float): ...
  """


class OpArgs(collections.namedtuple(
    'OpArgs', [
        'type',
        'initial_metadata',
        'trailing_metadata',
        'message',
        'status',
        'flags',
    ])):
  """Arguments passed into a GRPC operation.

  Attributes:
    type (OpType): ...
    initial_metadata (sequence of 2-sequence of str): Only valid if type ==
      OpType.SEND_INITIAL_METADATA, else is None.
    trailing_metadata (sequence of 2-sequence of str): Only valid if type ==
      OpType.SEND_STATUS_FROM_SERVER, else is None.
    message (bytes): Only valid if type == OpType.SEND_MESSAGE, else is None.
    status (Status): Only valid if type == OpType.SEND_STATUS_FROM_SERVER, else
      is None.
    flags (int): a bitwise OR'ing of 0 or more OpWriteFlags values.
  """

  @staticmethod
  def send_initial_metadata(initial_metadata):
    return OpArgs(OpType.SEND_INITIAL_METADATA, initial_metadata, None, None, None, 0)

  @staticmethod
  def send_message(message, flags):
    return OpArgs(OpType.SEND_MESSAGE, None, None, message, None, flags)

  @staticmethod
  def send_close_from_client():
    return OpArgs(OpType.SEND_CLOSE_FROM_CLIENT, None, None, None, None, 0)

  @staticmethod
  def send_status_from_server(trailing_metadata, status_code, status_details):
    return OpArgs(OpType.SEND_STATUS_FROM_SERVER, None, trailing_metadata, None, Status(status_code, status_details), 0)

  @staticmethod
  def recv_initial_metadata():
    return OpArgs(OpType.RECV_INITIAL_METADATA, None, None, None, None, 0);

  @staticmethod
  def recv_message():
    return OpArgs(OpType.RECV_MESSAGE, None, None, None, None, 0)

  @staticmethod
  def recv_status_on_client():
    return OpArgs(OpType.RECV_STATUS_ON_CLIENT, None, None, None, None, 0)

  @staticmethod
  def recv_close_on_server():
    return OpArgs(OpType.RECV_CLOSE_ON_SERVER, None, None, None, None, 0)


class OpResult(collections.namedtuple(
    'OpResult', [
        'type',
        'initial_metadata',
        'trailing_metadata',
        'message',
        'status',
        'cancelled',
    ])):
  """Results received from a GRPC operation.

  Attributes:
    type (OpType): ...
    initial_metadata (sequence of 2-sequence of str): Only valid if type ==
      OpType.RECV_INITIAL_METADATA, else is None.
    trailing_metadata (sequence of 2-sequence of str): Only valid if type ==
      OpType.RECV_STATUS_ON_CLIENT, else is None.
    message (bytes): Only valid if type == OpType.RECV_MESSAGE, else is None.
    status (Status): Only valid if type == OpType.RECV_STATUS_ON_CLIENT, else
      is None.
    cancelled (bool): Only valid if type == OpType.RECV_CLOSE_ON_SERVER, else
      is None.
  """


class Event(collections.namedtuple(
    'Event', [
        'type',
        'tag',
        'call',
        'call_details',
        'results',
        'success',
    ])):
  """An event received from a GRPC completion queue.

  Attributes:
    type (EventType): ...
    tag (object): ...
    call (Call): The Call object associated with this event (if there is one,
      else None).
    call_details (CallDetails): The call details associated with the
      server-side call (if there is such information, else None).
    results (list of OpResult): ...
    success (bool): ...
  """


class CompletionQueue(six.with_metaclass(abc.ABCMeta)):

  @abc.abstractmethod
  def __init__(self):
    pass

  def __iter__(self):
    """This class may be iterated over.

    This is the equivalent of calling next() repeatedly with an absolute
    deadline of None (i.e. no deadline).
    """
    return self

  def __next__(self):
    return self.next()

  @abc.abstractmethod
  def next(self, deadline=float('+inf')):
    """Get the next event on this completion queue.

    Args:
      deadline (float): absolute deadline in seconds from the Python epoch, or
        None for no deadline.

    Returns:
      Event: ...
    """
    pass

  @abc.abstractmethod
  def shutdown(self):
    """Begin the shutdown process of this completion queue.

    Note that this does not immediately destroy the completion queue.
    Nevertheless, user code should not pass it around after invoking this.
    """
    return None


class Call(six.with_metaclass(abc.ABCMeta)):

  @abc.abstractmethod
  def start_batch(self, ops, tag):
    """Start a batch of operations.

    Args:
      ops (sequence of OpArgs): ...
      tag (object): ...

    Returns:
      CallError: ...
    """
    return CallError.ERROR

  @abc.abstractmethod
  def cancel(self, code=None, details=None):
    """Cancel the call.

    Args:
      code (int): Status code to cancel with (on the server side). If
        specified, so must `details`.
      details (str): Status details to cancel with (on the server side). If
        specified, so must `code`.

    Returns:
      CallError: ...
    """
    return CallError.ERROR

  @abc.abstractmethod
  def peer(self):
    """Get the peer of this call.

    Returns:
      str: the peer of this call.
    """
    return None

  def set_credentials(self, creds):
    """Set per-call credentials.

    Args:
      creds (CallCredentials): Credentials to be set for this call.
    """
    return None


class Channel(six.with_metaclass(abc.ABCMeta)):

  @abc.abstractmethod
  def __init__(self, target, args, credentials=None):
    """Initialize a Channel.

    Args:
      target (str): ...
      args (sequence of 2-sequence of str, (str|integer)): ...
      credentials (ChannelCredentials): If None, create an insecure channel,
        else create a secure channel using the client credentials.
    """

  @abc.abstractmethod
  def create_call(self, completion_queue, method, host, deadline=float('+inf')):
    """Create a call from this channel.

    Args:
      completion_queue (CompletionQueue): ...
      method (str): ...
      host (str): ...
      deadline (float): absolute deadline in seconds from the Python epoch, or
        None for no deadline.

    Returns:
      Call: call object associated with this Channel and passed parameters.
    """
    return None

  @abc.abstractmethod
  def check_connectivity_state(self, try_to_connect):
    """Check and optionally repair the connectivity state of the channel.

    Args:
      try_to_connect (bool): whether or not to try to connect the channel if
      disconnected.

    Returns:
      ConnectivityState: state of the channel at the time of this invocation.
    """
    return None

  @abc.abstractmethod
  def watch_connectivity_state(self, last_observed_state, deadline,
                               completion_queue, tag):
    """Watch for connectivity state changes from the last_observed_state.

    Args:
      last_observed_state (ConnectivityState): ...
      deadline (float): ...
      completion_queue (CompletionQueue): ...
      tag (object) ...
    """

  @abc.abstractmethod
  def target(self):
    """Get the target of this channel.

    Returns:
      str: the target of this channel.
    """
    return None


class Server(six.with_metaclass(abc.ABCMeta)):

  @abc.abstractmethod
  def __init__(self, completion_queue, args):
    """Initialize a server.

    Args:
      completion_queue (CompletionQueue): ...
      args (sequence of 2-sequence of str, (str|integer)): ...
    """

  @abc.abstractmethod
  def add_http2_port(self, address, credentials=None):
    """Adds an HTTP/2 address+port to the server.

    Args:
      address (str): ...
      credentials (ServerCredentials): If None, create an insecure port, else
        create a secure port using the server credentials.
    """

  @abc.abstractmethod
  def start(self):
    """Starts the server."""

  @abc.abstractmethod
  def shutdown(self, tag=None):
    """Shuts down the server. Does not immediately destroy the server.

    Args:
      tag (object): if not None, have the server place an event on its
        completion queue notifying it when this server has completely shut down.
    """

  @abc.abstractmethod
  def request_call(self, completion_queue, tag):
    """Requests a call from the server on the server's completion queue.

    Args:
      completion_queue (CompletionQueue): Completion queue for the call. May be
        the same as the server's completion queue.
      tag (object) ...
    """
