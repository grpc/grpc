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

"""Utilities helpful for working with the base layer of RPC Framework."""

import collections
import threading

from grpc.framework.base import interfaces


class _ServicedSubscription(
    collections.namedtuple('_ServicedSubscription', ['kind', 'ingestor']),
    interfaces.ServicedSubscription):
  """See interfaces.ServicedSubscription for specification."""

_NONE_SUBSCRIPTION = _ServicedSubscription(
    interfaces.ServicedSubscription.Kind.NONE, None)
_TERMINATION_ONLY_SUBSCRIPTION = _ServicedSubscription(
    interfaces.ServicedSubscription.Kind.TERMINATION_ONLY, None)


def none_serviced_subscription():
  """Creates a "none" interfaces.ServicedSubscription object.

  Returns:
    An interfaces.ServicedSubscription indicating no subscription to an
      operation's results (such as would be the case for a fire-and-forget
      operation invocation).
  """
  return _NONE_SUBSCRIPTION


def termination_only_serviced_subscription():
  """Creates a "termination only" interfaces.ServicedSubscription object.

  Returns:
    An interfaces.ServicedSubscription indicating that the front-side customer
      is interested only in the overall termination outcome of the operation
      (such as completion or expiration) and would ignore the actual results of
      the operation.
  """
  return _TERMINATION_ONLY_SUBSCRIPTION


def full_serviced_subscription(ingestor):
  """Creates a "full" interfaces.ServicedSubscription object.

  Args:
    ingestor: An interfaces.ServicedIngestor.

  Returns:
    An interfaces.ServicedSubscription object indicating a full
      subscription.
  """
  return _ServicedSubscription(
      interfaces.ServicedSubscription.Kind.FULL, ingestor)


def wait_for_idle(end):
  """Waits for an interfaces.End to complete all operations.

  Args:
    end: Any interfaces.End.
  """
  event = threading.Event()
  end.add_idle_action(event.set)
  event.wait()
