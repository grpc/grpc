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
"""Code for instructing systems under test to block or fail."""

import abc
import contextlib
import threading

import six


class Defect(Exception):
    """Simulates a programming defect raised into in a system under test.

  Use of a standard exception type is too easily misconstrued as an actual
  defect in either the test infrastructure or the system under test.
  """


class Control(six.with_metaclass(abc.ABCMeta)):
    """An object that accepts program control from a system under test.

  Systems under test passed a Control should call its control() method
  frequently during execution. The control() method may block, raise an
  exception, or do nothing, all according to the enclosing test's desire for
  the system under test to simulate hanging, failing, or functioning.
  """

    @abc.abstractmethod
    def control(self):
        """Potentially does anything."""
        raise NotImplementedError()


class PauseFailControl(Control):
    """A Control that can be used to pause or fail code under control.

  This object is only safe for use from two threads: one of the system under
  test calling control and the other from the test system calling pause,
  block_until_paused, and fail.
  """

    def __init__(self):
        self._condition = threading.Condition()
        self._pause = False
        self._paused = False
        self._fail = False

    def control(self):
        with self._condition:
            if self._fail:
                raise Defect()

            while self._pause:
                self._paused = True
                self._condition.notify_all()
                self._condition.wait()
            self._paused = False

    @contextlib.contextmanager
    def pause(self):
        """Pauses code under control while controlling code is in context."""
        with self._condition:
            self._pause = True
        yield
        with self._condition:
            self._pause = False
            self._condition.notify_all()

    def block_until_paused(self):
        """Blocks controlling code until code under control is paused.

    May only be called within the context of a pause call.
    """
        with self._condition:
            while not self._paused:
                self._condition.wait()

    @contextlib.contextmanager
    def fail(self):
        """Fails code under control while controlling code is in context."""
        with self._condition:
            self._fail = True
        yield
        with self._condition:
            self._fail = False
