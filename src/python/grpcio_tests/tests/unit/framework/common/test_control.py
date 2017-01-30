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
