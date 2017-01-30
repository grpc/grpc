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
"""Utilities for testing stream-related code."""

from grpc.framework.foundation import stream


class TestConsumer(stream.Consumer):
    """A stream.Consumer instrumented for testing.

  Attributes:
    calls: A sequence of value-termination pairs describing the history of calls
      made on this object.
  """

    def __init__(self):
        self.calls = []

    def consume(self, value):
        """See stream.Consumer.consume for specification."""
        self.calls.append((value, False))

    def terminate(self):
        """See stream.Consumer.terminate for specification."""
        self.calls.append((None, True))

    def consume_and_terminate(self, value):
        """See stream.Consumer.consume_and_terminate for specification."""
        self.calls.append((value, True))

    def is_legal(self):
        """Reports whether or not a legal sequence of calls has been made."""
        terminated = False
        for value, terminal in self.calls:
            if terminated:
                return False
            elif terminal:
                terminated = True
            elif value is None:
                return False
        else:  # pylint: disable=useless-else-on-loop
            return True

    def values(self):
        """Returns the sequence of values that have been passed to this Consumer."""
        return [value for value, _ in self.calls if value]
