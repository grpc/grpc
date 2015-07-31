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

"""Interfaces defined and used by the base layer of RPC Framework."""

import abc
import collections
import enum

# stream is referenced from specification in this module.
from grpc.framework.foundation import stream  # pylint: disable=unused-import


@enum.unique
class Outcome(enum.Enum):
  """Operation outcomes."""

  COMPLETED = 'completed'
  CANCELLED = 'cancelled'
  EXPIRED = 'expired'
  RECEPTION_FAILURE = 'reception failure'
  TRANSMISSION_FAILURE = 'transmission failure'
  SERVICER_FAILURE = 'servicer failure'
  SERVICED_FAILURE = 'serviced failure'


class OperationContext(object):
  """Provides operation-related information and action.

  Attributes:
    trace_id: A uuid.UUID identifying a particular set of related operations.
  """
  __metaclass__ = abc.ABCMeta

  @abc.abstractmethod
  def is_active(self):
    """Describes whether the operation is active or has terminated."""
    raise NotImplementedError()

  @abc.abstractmethod
  def add_termination_callback(self, callback):
    """Adds a function to be called upon operation termination.

    Args:
      callback: A callable that will be passed an Outcome value.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def time_remaining(self):
    """Describes the length of allowed time remaining for the operation.

    Returns:
      A nonnegative float indicating the length of allowed time in seconds
      remaining for the operation to complete before it is considered to have
      timed out.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def fail(self, exception):
    """Indicates that the operation has failed.

    Args:
      exception: An exception germane to the operation failure. May be None.
    """
    raise NotImplementedError()


class Servicer(object):
  """Interface for service implementations."""
  __metaclass__ = abc.ABCMeta

  @abc.abstractmethod
  def service(self, name, context, output_consumer):
    """Services an operation.

    Args:
      name: The name of the operation.
      context: A ServicerContext object affording contextual information and
        actions.
      output_consumer: A stream.Consumer that will accept output values of
        the operation.

    Returns:
      A stream.Consumer that will accept input values for the operation.

    Raises:
      exceptions.NoSuchMethodError: If this Servicer affords no method with the
        given name.
      abandonment.Abandoned: If the operation has been aborted and there no
        longer is any reason to service the operation.
    """
    raise NotImplementedError()


class Operation(object):
  """Representation of an in-progress operation.

  Attributes:
    consumer: A stream.Consumer into which payloads constituting the operation's
      input may be passed.
    context: An OperationContext affording information and action about the
      operation.
  """
  __metaclass__ = abc.ABCMeta

  @abc.abstractmethod
  def cancel(self):
    """Cancels this operation."""
    raise NotImplementedError()


class ServicedIngestor(object):
  """Responsible for accepting the result of an operation."""
  __metaclass__ = abc.ABCMeta

  @abc.abstractmethod
  def consumer(self, operation_context):
    """Affords a consumer to which operation results will be passed.

    Args:
      operation_context: An OperationContext object for the current operation.

    Returns:
      A stream.Consumer to which the results of the current operation will be
        passed.

    Raises:
      abandonment.Abandoned: If the operation has been aborted and there no
        longer is any reason to service the operation.
    """
    raise NotImplementedError()


class ServicedSubscription(object):
  """A sum type representing a serviced's interest in an operation.

  Attributes:
    kind: A Kind value.
    ingestor: A ServicedIngestor. Must be present if kind is Kind.FULL. Must
      be None if kind is Kind.TERMINATION_ONLY or Kind.NONE.
  """
  __metaclass__ = abc.ABCMeta

  @enum.unique
  class Kind(enum.Enum):
    """Kinds of subscription."""

    FULL = 'full'
    TERMINATION_ONLY = 'termination only'
    NONE = 'none'


class End(object):
  """Common type for entry-point objects on both sides of an operation."""
  __metaclass__ = abc.ABCMeta

  @abc.abstractmethod
  def operation_stats(self):
    """Reports the number of terminated operations broken down by outcome.

    Returns:
      A dictionary from Outcome value to an integer identifying the number
        of operations that terminated with that outcome.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def add_idle_action(self, action):
    """Adds an action to be called when this End has no ongoing operations.

    Args:
      action: A callable that accepts no arguments.
    """
    raise NotImplementedError()


class Front(End):
  """Clientish objects that afford the invocation of operations."""
  __metaclass__ = abc.ABCMeta

  @abc.abstractmethod
  def operate(
      self, name, payload, complete, timeout, subscription, trace_id):
    """Commences an operation.

    Args:
      name: The name of the method invoked for the operation.
      payload: An initial payload for the operation. May be None.
      complete: A boolean indicating whether or not additional payloads to be
        sent to the servicer may be supplied after this call.
      timeout: A length of time in seconds to allow for the operation.
      subscription: A ServicedSubscription for the operation.
      trace_id: A uuid.UUID identifying a set of related operations to which
        this operation belongs.

    Returns:
      An Operation object affording information and action about the operation
        in progress.
    """
    raise NotImplementedError()


class Back(End):
  """Serverish objects that perform the work of operations."""
  __metaclass__ = abc.ABCMeta


class FrontToBackTicket(
    collections.namedtuple(
        'FrontToBackTicket',
        ['operation_id', 'sequence_number', 'kind', 'name', 'subscription',
         'trace_id', 'payload', 'timeout'])):
  """A sum type for all values sent from a front to a back.

  Attributes:
    operation_id: A unique-with-respect-to-equality hashable object identifying
      a particular operation.
    sequence_number: A zero-indexed integer sequence number identifying the
      ticket's place among all the tickets sent from front to back for this
      particular operation. Must be zero if kind is Kind.COMMENCEMENT or
      Kind.ENTIRE. Must be positive for any other kind.
    kind: A Kind value describing the overall kind of ticket.
    name: The name of an operation. Must be present if kind is Kind.COMMENCEMENT
      or Kind.ENTIRE. Must be None for any other kind.
    subscription: An ServicedSubscription.Kind value describing the interest
      the front has in tickets sent from the back. Must be present if
      kind is Kind.COMMENCEMENT or Kind.ENTIRE. Must be None for any other kind.
    trace_id: A uuid.UUID identifying a set of related operations to which this
      operation belongs. May be None.
    payload: A customer payload object. Must be present if kind is
      Kind.CONTINUATION. Must be None if kind is Kind.CANCELLATION. May be None
      for any other kind.
    timeout: An optional length of time (measured from the beginning of the
      operation) to allow for the entire operation. If None, a default value on
      the back will be used. If present and excessively large, the back may
      limit the operation to a smaller duration of its choice. May be present
      for any ticket kind; setting a value on a later ticket allows fronts
      to request time extensions (or even time reductions!) on in-progress
      operations.
  """

  @enum.unique
  class Kind(enum.Enum):
    """Identifies the overall kind of a FrontToBackTicket."""

    COMMENCEMENT = 'commencement'
    CONTINUATION = 'continuation'
    COMPLETION = 'completion'
    ENTIRE = 'entire'
    CANCELLATION = 'cancellation'
    EXPIRATION = 'expiration'
    SERVICER_FAILURE = 'servicer failure'
    SERVICED_FAILURE = 'serviced failure'
    RECEPTION_FAILURE = 'reception failure'
    TRANSMISSION_FAILURE = 'transmission failure'


class BackToFrontTicket(
    collections.namedtuple(
        'BackToFrontTicket',
        ['operation_id', 'sequence_number', 'kind', 'payload'])):
  """A sum type for all values sent from a back to a front.

  Attributes:
    operation_id: A unique-with-respect-to-equality hashable object identifying
      a particular operation.
    sequence_number: A zero-indexed integer sequence number identifying the
      ticket's place among all the tickets sent from back to front for this
      particular operation.
    kind: A Kind value describing the overall kind of ticket.
    payload: A customer payload object. Must be present if kind is
      Kind.CONTINUATION. May be None if kind is Kind.COMPLETION. Must be None
      otherwise.
  """

  @enum.unique
  class Kind(enum.Enum):
    """Identifies the overall kind of a BackToFrontTicket."""

    CONTINUATION = 'continuation'
    COMPLETION = 'completion'
    CANCELLATION = 'cancellation'
    EXPIRATION = 'expiration'
    SERVICER_FAILURE = 'servicer failure'
    SERVICED_FAILURE = 'serviced failure'
    RECEPTION_FAILURE = 'reception failure'
    TRANSMISSION_FAILURE = 'transmission failure'


class ForeLink(object):
  """Accepts back-to-front tickets and emits front-to-back tickets."""
  __metaclass__ = abc.ABCMeta

  @abc.abstractmethod
  def accept_back_to_front_ticket(self, ticket):
    """Accept a BackToFrontTicket.

    Args:
      ticket: Any BackToFrontTicket.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def join_rear_link(self, rear_link):
    """Mates this object with a peer with which it will exchange tickets."""
    raise NotImplementedError()


class RearLink(object):
  """Accepts front-to-back tickets and emits back-to-front tickets."""
  __metaclass__ = abc.ABCMeta

  @abc.abstractmethod
  def accept_front_to_back_ticket(self, ticket):
    """Accepts a FrontToBackTicket.

    Args:
      ticket: Any FrontToBackTicket.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def join_fore_link(self, fore_link):
    """Mates this object with a peer with which it will exchange tickets."""
    raise NotImplementedError()


class FrontLink(Front, ForeLink):
  """Clientish objects that operate by sending and receiving tickets."""
  __metaclass__ = abc.ABCMeta


class BackLink(Back, RearLink):
  """Serverish objects that operate by sending and receiving tickets."""
  __metaclass__ = abc.ABCMeta
