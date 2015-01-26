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

# TODO(nathaniel): Use Python's new enum library for enumerated types rather
# than constants merely placed close together.

import abc

# stream is referenced from specification in this module.
from _framework.foundation import stream  # pylint: disable=unused-import

# Operation outcomes.
COMPLETED = 'completed'
CANCELLED = 'cancelled'
EXPIRED = 'expired'
RECEPTION_FAILURE = 'reception failure'
TRANSMISSION_FAILURE = 'transmission failure'
SERVICER_FAILURE = 'servicer failure'
SERVICED_FAILURE = 'serviced failure'

# Subscription categories.
FULL = 'full'
TERMINATION_ONLY = 'termination only'
NONE = 'none'


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
      callback: A callable that will be passed one of COMPLETED, CANCELLED,
        EXPIRED, RECEPTION_FAILURE, TRANSMISSION_FAILURE, SERVICER_FAILURE, or
        SERVICED_FAILURE.
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
    category: One of FULL, TERMINATION_ONLY, or NONE.
    ingestor: A ServicedIngestor. Must be present if category is FULL.
  """
  __metaclass__ = abc.ABCMeta


class End(object):
  """Common type for entry-point objects on both sides of an operation."""
  __metaclass__ = abc.ABCMeta

  @abc.abstractmethod
  def operation_stats(self):
    """Reports the number of terminated operations broken down by outcome.

    Returns:
      A dictionary from operation outcome constant (COMPLETED, CANCELLED,
        EXPIRED, and so on) to an integer representing the number of operations
        that terminated with that outcome.
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
