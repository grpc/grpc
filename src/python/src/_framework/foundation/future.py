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

"""The Future interface missing from Python's standard library.

Python's concurrent.futures library defines a Future class very much like the
Future defined here, but since that class is concrete and without construction
semantics it is only available within the concurrent.futures library itself.
The Future class defined here is an entirely abstract interface that anyone may
implement and use.
"""

import abc
import collections

RETURNED = object()
RAISED = object()
ABORTED = object()


class Outcome(object):
  """A sum type describing the outcome of some computation.

  Attributes:
    category: One of RETURNED, RAISED, or ABORTED, respectively indicating
      that the computation returned a value, raised an exception, or was
      aborted.
    return_value: The value returned by the computation. Must be present if
      category is RETURNED.
    exception: The exception raised by the computation. Must be present if
      category is RAISED.
  """
  __metaclass__ = abc.ABCMeta


class _EasyOutcome(
    collections.namedtuple('_EasyOutcome',
                           ['category', 'return_value', 'exception']),
    Outcome):
  """A trivial implementation of Outcome."""

# All Outcomes describing abortion are indistinguishable so there might as well
# be only one.
_ABORTED_OUTCOME = _EasyOutcome(ABORTED, None, None)


def aborted():
  """Returns an Outcome indicating that a computation was aborted.

  Returns:
    An Outcome indicating that a computation was aborted.
  """
  return _ABORTED_OUTCOME


def raised(exception):
  """Returns an Outcome indicating that a computation raised an exception.

  Args:
    exception: The exception raised by the computation.

  Returns:
    An Outcome indicating that a computation raised the given exception.
  """
  return _EasyOutcome(RAISED, None, exception)


def returned(value):
  """Returns an Outcome indicating that a computation returned a value.

  Args:
    value: The value returned by the computation.

  Returns:
    An Outcome indicating that a computation returned the given value.
  """
  return _EasyOutcome(RETURNED, value, None)


class Future(object):
  """A representation of a computation happening in another control flow.

  Computations represented by a Future may have already completed, may be
  ongoing, or may be yet to be begun.

  Computations represented by a Future are considered uninterruptable; once
  started they will be allowed to terminate either by returning or raising
  an exception.
  """
  __metaclass__ = abc.ABCMeta

  @abc.abstractmethod
  def cancel(self):
    """Attempts to cancel the computation.

    Returns:
      True if the computation will not be allowed to take place or False if
        the computation has already taken place or is currently taking place.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def cancelled(self):
    """Describes whether the computation was cancelled.

    Returns:
      True if the computation was cancelled and did not take place or False
        if the computation took place, is taking place, or is scheduled to
        take place in the future.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def done(self):
    """Describes whether the computation has taken place.

    Returns:
      True if the computation took place; False otherwise.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def outcome(self):
    """Accesses the outcome of the computation.

    If the computation has not yet completed, this method blocks until it has.

    Returns:
      An Outcome describing the outcome of the computation.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def add_done_callback(self, callback):
    """Adds a function to be called at completion of the computation.

    The callback will be passed an Outcome object describing the outcome of
    the computation.

    If the computation has already completed, the callback will be called
    immediately.

    Args:
      callback: A callable taking an Outcome as its single parameter.
    """
    raise NotImplementedError()
