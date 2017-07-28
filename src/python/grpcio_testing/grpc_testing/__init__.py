# Copyright 2017 gRPC authors.
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
"""Objects for use in testing gRPC Python-using application code."""

import abc

import six

import grpc


class Time(six.with_metaclass(abc.ABCMeta)):
    """A simulation of time.

    Implementations needn't be connected with real time as provided by the
    Python interpreter, but as long as systems under test use
    RpcContext.is_active and RpcContext.time_remaining for querying RPC liveness
    implementations may be used to change passage of time in tests.
    """

    @abc.abstractmethod
    def time(self):
        """Accesses the current test time.

        Returns:
          The current test time (over which this object has authority).
        """
        raise NotImplementedError()

    @abc.abstractmethod
    def call_in(self, behavior, delay):
        """Adds a behavior to be called after some time.

        Args:
          behavior: A behavior to be called with no arguments.
          delay: A duration of time in seconds after which to call the behavior.

        Returns:
          A grpc.Future with which the call of the behavior may be cancelled
            before it is executed.
        """
        raise NotImplementedError()

    @abc.abstractmethod
    def call_at(self, behavior, time):
        """Adds a behavior to be called at a specific time.

        Args:
          behavior: A behavior to be called with no arguments.
          time: The test time at which to call the behavior.

        Returns:
          A grpc.Future with which the call of the behavior may be cancelled
            before it is executed.
        """
        raise NotImplementedError()

    @abc.abstractmethod
    def sleep_for(self, duration):
        """Blocks for some length of test time.

        Args:
          duration: A duration of test time in seconds for which to block.
        """
        raise NotImplementedError()

    @abc.abstractmethod
    def sleep_until(self, time):
        """Blocks until some test time.

        Args:
          time: The test time until which to block.
        """
        raise NotImplementedError()


def strict_real_time():
    """Creates a Time backed by the Python interpreter's time.

    The returned instance will be "strict" with respect to callbacks
    submitted to it: it will ensure that all callbacks registered to
    be called at time t have been called before it describes the time
    as having advanced beyond t.

    Returns:
      A Time backed by the "system" (Python interpreter's) time.
    """
    from grpc_testing import _time
    return _time.StrictRealTime()


def strict_fake_time(now):
    """Creates a Time that can be manipulated by test code.

    The returned instance maintains an internal representation of time
    independent of real time. This internal representation only advances
    when user code calls the instance's sleep_for and sleep_until methods.

    The returned instance will be "strict" with respect to callbacks
    submitted to it: it will ensure that all callbacks registered to
    be called at time t have been called before it describes the time
    as having advanced beyond t.

    Returns:
      A Time that simulates the passage of time.
    """
    from grpc_testing import _time
    return _time.StrictFakeTime(now)
