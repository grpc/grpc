# Copyright 2015 gRPC authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
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
