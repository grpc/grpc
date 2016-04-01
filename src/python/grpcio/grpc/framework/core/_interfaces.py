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

import six

from grpc.framework.interfaces.base import base


class TerminationManager(six.with_metaclass(abc.ABCMeta)):
  """An object responsible for handling the termination of an operation.

  Attributes:
    outcome: None if the operation is active or a base.Outcome value if it has
      terminated.
  """

  @abc.abstractmethod
  def add_callback(self, callback):
    """Registers a callback to be called on operation termination.

    If the operation has already terminated the callback will not be called.

    Args:
      callback: A callable that will be passed a base.Outcome value.

    Returns:
      None if the operation has not yet terminated and the passed callback will
        be called when it does, or a base.Outcome value describing the
        operation termination if the operation has terminated and the callback
        will not be called as a result of this method call.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def emission_complete(self):
    """Indicates that emissions from customer code have completed."""
    raise NotImplementedError()

  @abc.abstractmethod
  def transmission_complete(self):
    """Indicates that transmissions to the remote end are complete.

    Returns:
      True if the operation has terminated or False if the operation remains
        ongoing.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def reception_complete(self, code, details):
    """Indicates that reception from the other side is complete.

    Args:
      code: An application-specific code value.
      details: An application-specific details value.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def ingestion_complete(self):
    """Indicates that customer code ingestion of received values is complete."""
    raise NotImplementedError()

  @abc.abstractmethod
  def expire(self):
    """Indicates that the operation must abort because it has taken too long."""
    raise NotImplementedError()

  @abc.abstractmethod
  def abort(self, outcome):
    """Indicates that the operation must abort for the indicated reason.

    Args:
      outcome: A base.Outcome indicating operation abortion.
    """
    raise NotImplementedError()


class TransmissionManager(six.with_metaclass(abc.ABCMeta)):
  """A manager responsible for transmitting to the other end of an operation."""

  @abc.abstractmethod
  def kick_off(
      self, group, method, timeout, protocol_options, initial_metadata,
      payload, completion, allowance):
    """Transmits the values associated with operation invocation."""
    raise NotImplementedError()

  @abc.abstractmethod
  def advance(self, initial_metadata, payload, completion, allowance):
    """Accepts values for transmission to the other end of the operation.

    Args:
      initial_metadata: An initial metadata value to be transmitted to the other
        side of the operation. May only ever be non-None once.
      payload: A payload value.
      completion: A base.Completion value. May only ever be non-None in the last
        transmission to be made to the other side.
      allowance: A positive integer communicating the number of additional
        payloads allowed to be transmitted from the other side to this side of
        the operation, or None if no additional allowance is being granted in
        this call.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def timeout(self, timeout):
    """Accepts for transmission to the other side a new timeout value.

    Args:
      timeout: A positive float used as the new timeout value for the operation
        to be transmitted to the other side.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def allowance(self, allowance):
    """Indicates to this manager that the remote customer is allowing payloads.

    Args:
      allowance: A positive integer indicating the number of additional payloads
        the remote customer is allowing to be transmitted from this side of the
        operation.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def remote_complete(self):
    """Indicates to this manager that data from the remote side is complete."""
    raise NotImplementedError()

  @abc.abstractmethod
  def abort(self, outcome):
    """Indicates that the operation has aborted.

    Args:
      outcome: A base.Outcome for the operation. If None, indicates that the
        operation abortion should not be communicated to the other side of the
        operation.
    """
    raise NotImplementedError()


class ExpirationManager(six.with_metaclass(abc.ABCMeta)):
  """A manager responsible for aborting the operation if it runs out of time."""

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
  def terminate(self):
    """Indicates to this manager that the operation has terminated."""
    raise NotImplementedError()


class ProtocolManager(six.with_metaclass(abc.ABCMeta)):
  """A manager of protocol-specific values passing through an operation."""

  @abc.abstractmethod
  def set_protocol_receiver(self, protocol_receiver):
    """Registers the customer object that will receive protocol objects.

    Args:
      protocol_receiver: A base.ProtocolReceiver to which protocol objects for
        the operation should be passed.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def accept_protocol_context(self, protocol_context):
    """Accepts the protocol context object for the operation.

    Args:
      protocol_context: An object designated for use as the protocol context
        of the operation, with further semantics implementation-determined.
    """
    raise NotImplementedError()


class EmissionManager(six.with_metaclass(abc.ABCMeta, base.Operator)):
  """A manager of values emitted by customer code."""

  @abc.abstractmethod
  def advance(
      self, initial_metadata=None, payload=None, completion=None,
      allowance=None):
    """Accepts a value emitted by customer code.

    This method should only be called by customer code.

    Args:
      initial_metadata: An initial metadata value emitted by the local customer
        to be sent to the other side of the operation.
      payload: A payload value emitted by the local customer to be sent to the
        other side of the operation.
      completion: A Completion value emitted by the local customer to be sent to
        the other side of the operation.
      allowance: A positive integer indicating an additional number of payloads
        that the local customer is willing to accept from the other side of the
        operation.
    """
    raise NotImplementedError()


class IngestionManager(six.with_metaclass(abc.ABCMeta)):
  """A manager responsible for executing customer code.

  This name of this manager comes from its responsibility to pass successive
  values from the other side of the operation into the code of the local
  customer.
  """

  @abc.abstractmethod
  def set_group_and_method(self, group, method):
    """Communicates to this IngestionManager the operation group and method.

    Args:
      group: The group identifier of the operation.
      method: The method identifier of the operation.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def add_local_allowance(self, allowance):
    """Communicates to this IngestionManager that more payloads may be ingested.

    Args:
      allowance: A positive integer indicating an additional number of payloads
        that the local customer is willing to ingest.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def local_emissions_done(self):
    """Indicates to this manager that local emissions are done."""
    raise NotImplementedError()

  @abc.abstractmethod
  def advance(self, initial_metadata, payload, completion, allowance):
    """Advances the operation by passing values to the local customer."""
    raise NotImplementedError()


class ReceptionManager(six.with_metaclass(abc.ABCMeta)):
  """A manager responsible for receiving tickets from the other end."""

  @abc.abstractmethod
  def receive_ticket(self, ticket):
    """Handle a ticket from the other side of the operation.

    Args:
      ticket: A links.Ticket for the operation.
    """
    raise NotImplementedError()


class Operation(six.with_metaclass(abc.ABCMeta)):
  """An ongoing operation.

  Attributes:
    context: A base.OperationContext object for the operation.
    operator: A base.Operator object for the operation for use by the customer
      of the operation.
  """

  @abc.abstractmethod
  def handle_ticket(self, ticket):
    """Handle a ticket from the other side of the operation.

    Args:
      ticket: A links.Ticket from the other side of the operation.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def abort(self, outcome_kind):
    """Aborts the operation.

    Args:
      outcome_kind: A base.Outcome.Kind value indicating operation abortion.
    """
    raise NotImplementedError()
