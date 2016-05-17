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

"""Implementation of base.End."""

import abc
import threading
import uuid

import six

from grpc.framework.core import _operation
from grpc.framework.core import _utilities
from grpc.framework.foundation import callable_util
from grpc.framework.foundation import later
from grpc.framework.foundation import logging_pool
from grpc.framework.interfaces.base import base
from grpc.framework.interfaces.links import links
from grpc.framework.interfaces.links import utilities

_IDLE_ACTION_EXCEPTION_LOG_MESSAGE = 'Exception calling idle action!'


class End(six.with_metaclass(abc.ABCMeta, base.End, links.Link)):
  """A bridge between base.End and links.Link.

  Implementations of this interface translate arriving tickets into
  calls on application objects implementing base interfaces and
  translate calls from application objects implementing base interfaces
  into tickets sent to a joined link.
  """


class _Cycle(object):
  """State for a single start-stop End lifecycle."""

  def __init__(self, pool):
    self.pool = pool
    self.grace = False
    self.futures = []
    self.operations = {}
    self.idle_actions = []


def _abort(operations):
  for operation in operations:
    operation.abort(base.Outcome.Kind.LOCAL_SHUTDOWN)


def _cancel_futures(futures):
  for future in futures:
    future.cancel()


def _future_shutdown(lock, cycle, event):
  def in_future():
    with lock:
      _abort(cycle.operations.values())
      _cancel_futures(cycle.futures)
  return in_future


class _End(End):
  """An End implementation."""

  def __init__(self, servicer_package):
    """Constructor.

    Args:
      servicer_package: A _ServicerPackage for servicing operations or None if
        this end will not be used to service operations.
    """
    self._lock = threading.Condition()
    self._servicer_package = servicer_package

    self._stats = {outcome_kind: 0 for outcome_kind in base.Outcome.Kind}

    self._mate = None

    self._cycle = None

  def _termination_action(self, operation_id):
    """Constructs the termination action for a single operation.

    Args:
      operation_id: The operation ID for the termination action.

    Returns:
      A callable that takes an operation outcome kind as its sole parameter and
        that should be used as the termination action for the operation
        associated with the given operation ID.
    """
    def termination_action(outcome_kind):
      with self._lock:
        self._stats[outcome_kind] += 1
        self._cycle.operations.pop(operation_id, None)
        if not self._cycle.operations:
          for action in self._cycle.idle_actions:
            self._cycle.pool.submit(action)
          self._cycle.idle_actions = []
          if self._cycle.grace:
            _cancel_futures(self._cycle.futures)
            self._cycle.pool.shutdown(wait=False)
            self._cycle = None
    return termination_action

  def start(self):
    """See base.End.start for specification."""
    with self._lock:
      if self._cycle is not None:
        raise ValueError('Tried to start a not-stopped End!')
      else:
        self._cycle = _Cycle(logging_pool.pool(1))

  def stop(self, grace):
    """See base.End.stop for specification."""
    with self._lock:
      if self._cycle is None:
        event = threading.Event()
        event.set()
        return event
      elif not self._cycle.operations:
        event = threading.Event()
        self._cycle.pool.submit(event.set)
        self._cycle.pool.shutdown(wait=False)
        self._cycle = None
        return event
      else:
        self._cycle.grace = True
        event = threading.Event()
        self._cycle.idle_actions.append(event.set)
        if 0 < grace:
          future = later.later(
              grace, _future_shutdown(self._lock, self._cycle, event))
          self._cycle.futures.append(future)
        else:
          _abort(self._cycle.operations.values())
        return event

  def operate(
      self, group, method, subscription, timeout, initial_metadata=None,
      payload=None, completion=None, protocol_options=None):
    """See base.End.operate for specification."""
    operation_id = uuid.uuid4()
    with self._lock:
      if self._cycle is None or self._cycle.grace:
        raise ValueError('Can\'t operate on stopped or stopping End!')
      termination_action = self._termination_action(operation_id)
      operation = _operation.invocation_operate(
          operation_id, group, method, subscription, timeout, protocol_options,
          initial_metadata, payload, completion, self._mate.accept_ticket,
          termination_action, self._cycle.pool)
      self._cycle.operations[operation_id] = operation
      return operation.context, operation.operator

  def operation_stats(self):
    """See base.End.operation_stats for specification."""
    with self._lock:
      return dict(self._stats)

  def add_idle_action(self, action):
    """See base.End.add_idle_action for specification."""
    with self._lock:
      if self._cycle is None:
        raise ValueError('Can\'t add idle action to stopped End!')
      action_with_exceptions_logged = callable_util.with_exceptions_logged(
          action, _IDLE_ACTION_EXCEPTION_LOG_MESSAGE)
      if self._cycle.operations:
        self._cycle.idle_actions.append(action_with_exceptions_logged)
      else:
        self._cycle.pool.submit(action_with_exceptions_logged)

  def accept_ticket(self, ticket):
    """See links.Link.accept_ticket for specification."""
    with self._lock:
      if self._cycle is not None:
        operation = self._cycle.operations.get(ticket.operation_id)
        if operation is not None:
          operation.handle_ticket(ticket)
        elif self._servicer_package is not None and not self._cycle.grace:
          termination_action = self._termination_action(ticket.operation_id)
          operation = _operation.service_operate(
              self._servicer_package, ticket, self._mate.accept_ticket,
              termination_action, self._cycle.pool)
          if operation is not None:
            self._cycle.operations[ticket.operation_id] = operation

  def join_link(self, link):
    """See links.Link.join_link for specification."""
    with self._lock:
      self._mate = utilities.NULL_LINK if link is None else link


def serviceless_end_link():
  """Constructs an End usable only for invoking operations.

  Returns:
    An End usable for translating operations into ticket exchange.
  """
  return _End(None)


def serviceful_end_link(servicer, default_timeout, maximum_timeout):
  """Constructs an End capable of servicing operations.

  Args:
    servicer: An interfaces.Servicer for servicing operations.
    default_timeout: A length of time in seconds to be used as the default
      time alloted for a single operation.
    maximum_timeout: A length of time in seconds to be used as the maximum
      time alloted for a single operation.

  Returns:
    An End capable of servicing the operations requested of it through ticket
      exchange.
  """
  return _End(
      _utilities.ServicerPackage(servicer, default_timeout, maximum_timeout))
