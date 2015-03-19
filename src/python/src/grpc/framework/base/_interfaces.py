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

"""Package-internal interfaces."""

import abc

# interfaces is referenced from specification in this module.
from grpc.framework.base import interfaces  # pylint: disable=unused-import
from grpc.framework.foundation import stream


class TerminationManager(object):
  """An object responsible for handling the termination of an operation."""
  __metaclass__ = abc.ABCMeta

  @abc.abstractmethod
  def set_expiration_manager(self, expiration_manager):
    """Sets the ExpirationManager with which this object will cooperate."""
    raise NotImplementedError()

  @abc.abstractmethod
  def is_active(self):
    """Reports whether or not the operation is active.

    Returns:
      True if the operation is active or False if the operation has terminated.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def add_callback(self, callback):
    """Registers a callback to be called on operation termination.

    If the operation has already terminated, the callback will be called
    immediately.

    Args:
      callback: A callable that will be passed an interfaces.Outcome value.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def emission_complete(self):
    """Indicates that emissions from customer code have completed."""
    raise NotImplementedError()

  @abc.abstractmethod
  def transmission_complete(self):
    """Indicates that transmissions to the remote end are complete."""
    raise NotImplementedError()

  @abc.abstractmethod
  def ingestion_complete(self):
    """Indicates that customer code ingestion of received values is complete."""
    raise NotImplementedError()

  @abc.abstractmethod
  def abort(self, outcome):
    """Indicates that the operation must abort for the indicated reason.

    Args:
      outcome: An interfaces.Outcome indicating operation abortion.
    """
    raise NotImplementedError()


class TransmissionManager(object):
  """A manager responsible for transmitting to the other end of an operation."""
  __metaclass__ = abc.ABCMeta

  @abc.abstractmethod
  def inmit(self, emission, complete):
    """Accepts a value for transmission to the other end of the operation.

    Args:
      emission: A value of some significance to the customer to be transmitted
        to the other end of the operation. May be None only if complete is True.
      complete: A boolean that if True indicates that customer code has emitted
        all values it intends to emit.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def abort(self, outcome):
    """Indicates that the operation has aborted for the indicated reason.

    Args:
      outcome: An interfaces.Outcome indicating operation abortion.
    """
    raise NotImplementedError()


class EmissionManager(stream.Consumer):
  """A manager of values emitted by customer code."""
  __metaclass__ = abc.ABCMeta

  @abc.abstractmethod
  def set_ingestion_manager_and_expiration_manager(
      self, ingestion_manager, expiration_manager):
    """Sets two other objects with which this EmissionManager will cooperate.

    Args:
      ingestion_manager: The IngestionManager for the operation.
      expiration_manager: The ExpirationManager for the operation.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def consume(self, value):
    """Accepts a value emitted by customer code.

    This method should only be called by customer code.

    Args:
      value: Any value of significance to the customer.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def terminate(self):
    """Indicates that no more values will be emitted by customer code.

    This method should only be called by customer code.

    Implementations of this method may be idempotent and forgive customer code
    calling this method more than once.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def consume_and_terminate(self, value):
    """Accepts the last value emitted by customer code.

    This method should only be called by customer code.

    Args:
      value: Any value of significance to the customer.
    """
    raise NotImplementedError()


class IngestionManager(stream.Consumer):
  """A manager responsible for executing customer code."""
  __metaclass__ = abc.ABCMeta

  @abc.abstractmethod
  def set_expiration_manager(self, expiration_manager):
    """Sets the ExpirationManager with which this object will cooperate."""
    raise NotImplementedError()

  @abc.abstractmethod
  def start(self, requirement):
    """Commences execution of customer code.

    Args:
      requirement: Some value unavailable at the time of this object's
        construction that is required to begin executing customer code.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def consume(self, payload):
    """Accepts a customer-significant value to be supplied to customer code.

    Args:
      payload: Some customer-significant value.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def terminate(self):
    """Indicates the end of values to be supplied to customer code."""
    raise NotImplementedError()

  @abc.abstractmethod
  def consume_and_terminate(self, payload):
    """Accepts the last value to be supplied to customer code.

    Args:
      payload: Some customer-significant value (and the last such value).
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def abort(self):
    """Indicates to this manager that the operation has aborted."""
    raise NotImplementedError()


class ExpirationManager(object):
  """A manager responsible for aborting the operation if it runs out of time."""
  __metaclass__ = abc.ABCMeta

  @abc.abstractmethod
  def change_timeout(self, timeout):
    """Changes the timeout allotted for the operation.

    Operation duration is always measure from the beginning of the operation;
    calling this method changes the operation's allotted time to timeout total
    seconds, not timeout seconds from the time of this method call.

    Args:
      timeout: A length of time in seconds to allow for the operation.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def deadline(self):
    """Returns the time until which the operation is allowed to run.

    Returns:
      The time (seconds since the epoch) at which the operation will expire.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def abort(self):
    """Indicates to this manager that the operation has aborted."""
    raise NotImplementedError()


class ReceptionManager(object):
  """A manager responsible for receiving tickets from the other end."""
  __metaclass__ = abc.ABCMeta

  @abc.abstractmethod
  def receive_ticket(self, ticket):
    """Handle a ticket from the other side of the operation.

    Args:
      ticket: An interfaces.BackToFrontTicket or interfaces.FrontToBackTicket
        appropriate to this end of the operation and this object.
    """
    raise NotImplementedError()


class CancellationManager(object):
  """A manager of operation cancellation."""
  __metaclass__ = abc.ABCMeta

  @abc.abstractmethod
  def cancel(self):
    """Cancels the operation."""
    raise NotImplementedError()
