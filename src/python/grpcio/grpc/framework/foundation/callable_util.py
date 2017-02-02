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
"""Utilities for working with callables."""

import abc
import collections
import enum
import functools
import logging

import six


class Outcome(six.with_metaclass(abc.ABCMeta)):
    """A sum type describing the outcome of some call.

  Attributes:
    kind: One of Kind.RETURNED or Kind.RAISED respectively indicating that the
      call returned a value or raised an exception.
    return_value: The value returned by the call. Must be present if kind is
      Kind.RETURNED.
    exception: The exception raised by the call. Must be present if kind is
      Kind.RAISED.
  """

    @enum.unique
    class Kind(enum.Enum):
        """Identifies the general kind of the outcome of some call."""

        RETURNED = object()
        RAISED = object()


class _EasyOutcome(
        collections.namedtuple('_EasyOutcome',
                               ['kind', 'return_value', 'exception']), Outcome):
    """A trivial implementation of Outcome."""


def _call_logging_exceptions(behavior, message, *args, **kwargs):
    try:
        return _EasyOutcome(Outcome.Kind.RETURNED,
                            behavior(*args, **kwargs), None)
    except Exception as e:  # pylint: disable=broad-except
        logging.exception(message)
        return _EasyOutcome(Outcome.Kind.RAISED, None, e)


def with_exceptions_logged(behavior, message):
    """Wraps a callable in a try-except that logs any exceptions it raises.

  Args:
    behavior: Any callable.
    message: A string to log if the behavior raises an exception.

  Returns:
    A callable that when executed invokes the given behavior. The returned
      callable takes the same arguments as the given behavior but returns a
      future.Outcome describing whether the given behavior returned a value or
      raised an exception.
  """

    @functools.wraps(behavior)
    def wrapped_behavior(*args, **kwargs):
        return _call_logging_exceptions(behavior, message, *args, **kwargs)

    return wrapped_behavior


def call_logging_exceptions(behavior, message, *args, **kwargs):
    """Calls a behavior in a try-except that logs any exceptions it raises.

  Args:
    behavior: Any callable.
    message: A string to log if the behavior raises an exception.
    *args: Positional arguments to pass to the given behavior.
    **kwargs: Keyword arguments to pass to the given behavior.

  Returns:
    An Outcome describing whether the given behavior returned a value or raised
      an exception.
  """
    return _call_logging_exceptions(behavior, message, *args, **kwargs)
