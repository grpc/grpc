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

"""Utilities for use with the base interface of RPC Framework."""

import collections

from grpc.framework.interfaces.base import base


class _Completion(
    base.Completion,
    collections.namedtuple(
        '_Completion', ('terminal_metadata', 'code', 'message',))):
  """A trivial implementation of base.Completion."""


class _Subscription(
    base.Subscription,
    collections.namedtuple(
        '_Subscription',
        ('kind', 'termination_callback', 'allowance', 'operator',
         'protocol_receiver',))):
  """A trivial implementation of base.Subscription."""

_NONE_SUBSCRIPTION = _Subscription(
    base.Subscription.Kind.NONE, None, None, None, None)


def completion(terminal_metadata, code, message):
  """Creates a base.Completion aggregating the given operation values.

  Args:
    terminal_metadata: A terminal metadata value for an operaton.
    code: A code value for an operation.
    message: A message value for an operation.

  Returns:
    A base.Completion aggregating the given operation values.
  """
  return _Completion(terminal_metadata, code, message)


def full_subscription(operator, protocol_receiver):
  """Creates a "full" base.Subscription for the given base.Operator.

  Args:
    operator: A base.Operator to be used in an operation.
    protocol_receiver: A base.ProtocolReceiver to be used in an operation.

  Returns:
    A base.Subscription of kind base.Subscription.Kind.FULL wrapping the given
      base.Operator and base.ProtocolReceiver.
  """
  return _Subscription(
      base.Subscription.Kind.FULL, None, None, operator, protocol_receiver)
