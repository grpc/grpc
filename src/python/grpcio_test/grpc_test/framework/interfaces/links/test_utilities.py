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

"""State and behavior appropriate for use in tests."""

import logging
import threading
import time

from grpc.framework.interfaces.links import links
from grpc.framework.interfaces.links import utilities

# A more-or-less arbitrary limit on the length of raw data values to be logged.
_UNCOMFORTABLY_LONG = 48


def _safe_for_log_ticket(ticket):
  """Creates a safe-for-printing-to-the-log ticket for a given ticket.

  Args:
    ticket: Any links.Ticket.

  Returns:
    A links.Ticket that is as much as can be equal to the given ticket but
      possibly features values like the string "<payload of length 972321>" in
      place of the actual values of the given ticket.
  """
  if isinstance(ticket.payload, (basestring,)):
    payload_length = len(ticket.payload)
  else:
    payload_length = -1
  if payload_length < _UNCOMFORTABLY_LONG:
    return ticket
  else:
    return links.Ticket(
        ticket.operation_id, ticket.sequence_number,
        ticket.group, ticket.method, ticket.subscription, ticket.timeout,
        ticket.allowance, ticket.initial_metadata,
        '<payload of length {}>'.format(payload_length),
        ticket.terminal_metadata, ticket.code, ticket.message,
        ticket.termination, None)


class RecordingLink(links.Link):
  """A Link that records every ticket passed to it."""

  def __init__(self):
    self._condition = threading.Condition()
    self._tickets = []

  def accept_ticket(self, ticket):
    with self._condition:
      self._tickets.append(ticket)
      self._condition.notify_all()

  def join_link(self, link):
    pass

  def block_until_tickets_satisfy(self, predicate):
    """Blocks until the received tickets satisfy the given predicate.

    Args:
      predicate: A callable that takes a sequence of tickets and returns a
        boolean value.
    """
    with self._condition:
      while not predicate(self._tickets):
        self._condition.wait()

  def tickets(self):
    """Returns a copy of the list of all tickets received by this Link."""
    with self._condition:
      return tuple(self._tickets)


class _Pipe(object):
  """A conduit that logs all tickets passed through it."""

  def __init__(self, name):
    self._lock = threading.Lock()
    self._name = name
    self._left_mate = utilities.NULL_LINK
    self._right_mate = utilities.NULL_LINK

  def accept_left_to_right_ticket(self, ticket):
    with self._lock:
      logging.warning(
          '%s: moving left to right through %s: %s', time.time(), self._name,
          _safe_for_log_ticket(ticket))
      try:
        self._right_mate.accept_ticket(ticket)
      except Exception as e:  # pylint: disable=broad-except
        logging.exception(e)

  def accept_right_to_left_ticket(self, ticket):
    with self._lock:
      logging.warning(
          '%s: moving right to left through %s: %s', time.time(), self._name,
          _safe_for_log_ticket(ticket))
      try:
        self._left_mate.accept_ticket(ticket)
      except Exception as e:  # pylint: disable=broad-except
        logging.exception(e)

  def join_left_mate(self, left_mate):
    with self._lock:
      self._left_mate = utilities.NULL_LINK if left_mate is None else left_mate

  def join_right_mate(self, right_mate):
    with self._lock:
      self._right_mate = (
          utilities.NULL_LINK if right_mate is None else right_mate)


class _Facade(links.Link):

  def __init__(self, accept, join):
    self._accept = accept
    self._join = join

  def accept_ticket(self, ticket):
    self._accept(ticket)

  def join_link(self, link):
    self._join(link)


def logging_links(name):
  """Creates a conduit that logs all tickets passed through it.

  Args:
    name: A name to use for the conduit to identify itself in logging output.

  Returns:
    Two links.Links, the first of which is the "left" side of the conduit
      and the second of which is the "right" side of the conduit.
  """
  pipe = _Pipe(name)
  left_facade = _Facade(pipe.accept_left_to_right_ticket, pipe.join_left_mate)
  right_facade = _Facade(pipe.accept_right_to_left_ticket, pipe.join_right_mate)
  return left_facade, right_facade
