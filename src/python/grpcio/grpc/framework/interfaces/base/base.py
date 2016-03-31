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

"""The base interface of RPC Framework.

Implementations of this interface support the conduct of "operations":
exchanges between two distinct ends of an arbitrary number of data payloads
and metadata such as a name for the operation, initial and terminal metadata
in each direction, and flow control. These operations may be used for transfers
of data, remote procedure calls, status indication, or anything else
applications choose.
"""

# threading is referenced from specification in this module.
import abc
import enum
import threading  # pylint: disable=unused-import

import six

# abandonment is referenced from specification in this module.
from grpc.framework.foundation import abandonment  # pylint: disable=unused-import


class NoSuchMethodError(Exception):
  """Indicates that an unrecognized operation has been called.

  Attributes:
    code: A code value to communicate to the other side of the operation along
      with indication of operation termination. May be None.
    details: A details value to communicate to the other side of the operation
      along with indication of operation termination. May be None.
  """

  def __init__(self, code, details):
    """Constructor.

    Args:
      code: A code value to communicate to the other side of the operation
        along with indication of operation termination. May be None.
      details: A details value to communicate to the other side of the
        operation along with indication of operation termination. May be None.
    """
    self.code = code
    self.details = details


class Outcome(object):
  """The outcome of an operation.

  Attributes:
    kind: A Kind value coarsely identifying how the operation terminated.
    code: An application-specific code value or None if no such value was
      provided.
    details: An application-specific details value or None if no such value was
      provided.
  """

  @enum.unique
  class Kind(enum.Enum):
    """Ways in which an operation can terminate."""

    COMPLETED = 'completed'
    CANCELLED = 'cancelled'
    EXPIRED = 'expired'
    LOCAL_SHUTDOWN = 'local shutdown'
    REMOTE_SHUTDOWN = 'remote shutdown'
    RECEPTION_FAILURE = 'reception failure'
    TRANSMISSION_FAILURE = 'transmission failure'
    LOCAL_FAILURE = 'local failure'
    REMOTE_FAILURE = 'remote failure'


class Completion(six.with_metaclass(abc.ABCMeta)):
  """An aggregate of the values exchanged upon operation completion.

  Attributes:
    terminal_metadata: A terminal metadata value for the operaton.
    code: A code value for the operation.
    message: A message value for the operation.
  """


class OperationContext(six.with_metaclass(abc.ABCMeta)):
  """Provides operation-related information and action."""

  @abc.abstractmethod
  def outcome(self):
    """Indicates the operation's outcome (or that the operation is ongoing).

    Returns:
      None if the operation is still active or the Outcome value for the
        operation if it has terminated.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def add_termination_callback(self, callback):
    """Adds a function to be called upon operation termination.

    Args:
      callback: A callable to be passed an Outcome value on operation
        termination.

    Returns:
      None if the operation has not yet terminated and the passed callback will
        later be called when it does terminate, or if the operation has already
        terminated an Outcome value describing the operation termination and the
        passed callback will not be called as a result of this method call.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def time_remaining(self):
    """Describes the length of allowed time remaining for the operation.

    Returns:
      A nonnegative float indicating the length of allowed time in seconds
      remaining for the operation to complete before it is considered to have
      timed out. Zero is returned if the operation has terminated.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def cancel(self):
    """Cancels the operation if the operation has not yet terminated."""
    raise NotImplementedError()

  @abc.abstractmethod
  def fail(self, exception):
    """Indicates that the operation has failed.

    Args:
      exception: An exception germane to the operation failure. May be None.
    """
    raise NotImplementedError()


class Operator(six.with_metaclass(abc.ABCMeta)):
  """An interface through which to participate in an operation."""

  @abc.abstractmethod
  def advance(
      self, initial_metadata=None, payload=None, completion=None,
      allowance=None):
    """Progresses the operation.

    Args:
      initial_metadata: An initial metadata value. Only one may ever be
        communicated in each direction for an operation, and they must be
        communicated no later than either the first payload or the completion.
      payload: A payload value.
      completion: A Completion value. May only ever be non-None once in either
        direction, and no payloads may be passed after it has been communicated.
      allowance: A positive integer communicating the number of additional
        payloads allowed to be passed by the remote side of the operation.
    """
    raise NotImplementedError()

class ProtocolReceiver(six.with_metaclass(abc.ABCMeta)):
  """A means of receiving protocol values during an operation."""

  @abc.abstractmethod
  def context(self, protocol_context):
    """Accepts the protocol context object for the operation.

    Args:
      protocol_context: The protocol context object for the operation.
    """
    raise NotImplementedError()


class Subscription(six.with_metaclass(abc.ABCMeta)):
  """Describes customer code's interest in values from the other side.

  Attributes:
    kind: A Kind value describing the overall kind of this value.
    termination_callback: A callable to be passed the Outcome associated with
      the operation after it has terminated. Must be non-None if kind is
      Kind.TERMINATION_ONLY. Must be None otherwise.
    allowance: A callable behavior that accepts positive integers representing
      the number of additional payloads allowed to be passed to the other side
      of the operation. Must be None if kind is Kind.FULL. Must not be None
      otherwise.
    operator: An Operator to be passed values from the other side of the
      operation. Must be non-None if kind is Kind.FULL. Must be None otherwise.
    protocol_receiver: A ProtocolReceiver to be passed protocol objects as they
      become available during the operation. Must be non-None if kind is
      Kind.FULL.
  """

  @enum.unique
  class Kind(enum.Enum):

    NONE = 'none'
    TERMINATION_ONLY = 'termination only'
    FULL = 'full'


class Servicer(six.with_metaclass(abc.ABCMeta)):
  """Interface for service implementations."""

  @abc.abstractmethod
  def service(self, group, method, context, output_operator):
    """Services an operation.

    Args:
      group: The group identifier of the operation to be serviced.
      method: The method identifier of the operation to be serviced.
      context: An OperationContext object affording contextual information and
        actions.
      output_operator: An Operator that will accept output values of the
        operation.

    Returns:
      A Subscription via which this object may or may not accept more values of
        the operation.

    Raises:
      NoSuchMethodError: If this Servicer does not handle operations with the
        given group and method.
      abandonment.Abandoned: If the operation has been aborted and there no
        longer is any reason to service the operation.
    """
    raise NotImplementedError()


class End(six.with_metaclass(abc.ABCMeta)):
  """Common type for entry-point objects on both sides of an operation."""

  @abc.abstractmethod
  def start(self):
    """Starts this object's service of operations."""
    raise NotImplementedError()

  @abc.abstractmethod
  def stop(self, grace):
    """Stops this object's service of operations.

    This object will refuse service of new operations as soon as this method is
    called but operations under way at the time of the call may be given a
    grace period during which they are allowed to finish.

    Args:
      grace: A duration of time in seconds to allow ongoing operations to
        terminate before being forcefully terminated by the stopping of this
        End. May be zero to terminate all ongoing operations and immediately
        stop.

    Returns:
      A threading.Event that will be set to indicate all operations having
        terminated and this End having completely stopped. The returned event
        may not be set until after the full grace period (if some ongoing
        operation continues for the full length of the period) or it may be set
        much sooner (if for example this End had no operations in progress at
        the time its stop method was called).
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def operate(
      self, group, method, subscription, timeout, initial_metadata=None,
      payload=None, completion=None, protocol_options=None):
    """Commences an operation.

    Args:
      group: The group identifier of the invoked operation.
      method: The method identifier of the invoked operation.
      subscription: A Subscription to which the results of the operation will be
        passed.
      timeout: A length of time in seconds to allow for the operation.
      initial_metadata: An initial metadata value to be sent to the other side
        of the operation. May be None if the initial metadata will be later
        passed via the returned operator or if there will be no initial metadata
        passed at all.
      payload: An initial payload for the operation.
      completion: A Completion value indicating the end of transmission to the
        other side of the operation.
      protocol_options: A value specified by the provider of a Base interface
        implementation affording custom state and behavior.

    Returns:
      A pair of objects affording information about the operation and action
        continuing the operation. The first element of the returned pair is an
        OperationContext for the operation and the second element of the
        returned pair is an Operator to which operation values not passed in
        this call should later be passed.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def operation_stats(self):
    """Reports the number of terminated operations broken down by outcome.

    Returns:
      A dictionary from Outcome.Kind value to an integer identifying the number
        of operations that terminated with that outcome kind.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def add_idle_action(self, action):
    """Adds an action to be called when this End has no ongoing operations.

    Args:
      action: A callable that accepts no arguments.
    """
    raise NotImplementedError()
