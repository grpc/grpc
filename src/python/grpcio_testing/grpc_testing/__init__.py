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

from google.protobuf import descriptor
import six

import grpc


class UnaryUnaryChannelRpc(six.with_metaclass(abc.ABCMeta)):
    """Fixture for a unary-unary RPC invoked by a system under test.

    Enables users to "play server" for the RPC.
    """

    @abc.abstractmethod
    def send_initial_metadata(self, initial_metadata):
        """Sends the RPC's initial metadata to the system under test.

        Args:
          initial_metadata: The RPC's initial metadata to be "sent" to
            the system under test.
        """
        raise NotImplementedError()

    @abc.abstractmethod
    def cancelled(self):
        """Blocks until the system under test has cancelled the RPC."""
        raise NotImplementedError()

    @abc.abstractmethod
    def terminate(self, response, trailing_metadata, code, details):
        """Terminates the RPC.

        Args:
          response: The response for the RPC.
          trailing_metadata: The RPC's trailing metadata.
          code: The RPC's status code.
          details: The RPC's status details.
        """
        raise NotImplementedError()


class UnaryStreamChannelRpc(six.with_metaclass(abc.ABCMeta)):
    """Fixture for a unary-stream RPC invoked by a system under test.

    Enables users to "play server" for the RPC.
    """

    @abc.abstractmethod
    def send_initial_metadata(self, initial_metadata):
        """Sends the RPC's initial metadata to the system under test.

        Args:
          initial_metadata: The RPC's initial metadata to be "sent" to
            the system under test.
        """
        raise NotImplementedError()

    @abc.abstractmethod
    def send_response(self, response):
        """Sends a response to the system under test.

        Args:
          response: A response message to be "sent" to the system under test.
        """
        raise NotImplementedError()

    @abc.abstractmethod
    def cancelled(self):
        """Blocks until the system under test has cancelled the RPC."""
        raise NotImplementedError()

    @abc.abstractmethod
    def terminate(self, trailing_metadata, code, details):
        """Terminates the RPC.

        Args:
          trailing_metadata: The RPC's trailing metadata.
          code: The RPC's status code.
          details: The RPC's status details.
        """
        raise NotImplementedError()


class StreamUnaryChannelRpc(six.with_metaclass(abc.ABCMeta)):
    """Fixture for a stream-unary RPC invoked by a system under test.

    Enables users to "play server" for the RPC.
    """

    @abc.abstractmethod
    def send_initial_metadata(self, initial_metadata):
        """Sends the RPC's initial metadata to the system under test.

        Args:
          initial_metadata: The RPC's initial metadata to be "sent" to
            the system under test.
        """
        raise NotImplementedError()

    @abc.abstractmethod
    def take_request(self):
        """Draws one of the requests added to the RPC by the system under test.

        This method blocks until the system under test has added to the RPC
        the request to be returned.

        Successive calls to this method return requests in the same order in
        which the system under test added them to the RPC.

        Returns:
          A request message added to the RPC by the system under test.
        """
        raise NotImplementedError()

    @abc.abstractmethod
    def requests_closed(self):
        """Blocks until the system under test has closed the request stream."""
        raise NotImplementedError()

    @abc.abstractmethod
    def cancelled(self):
        """Blocks until the system under test has cancelled the RPC."""
        raise NotImplementedError()

    @abc.abstractmethod
    def terminate(self, response, trailing_metadata, code, details):
        """Terminates the RPC.

        Args:
          response: The response for the RPC.
          trailing_metadata: The RPC's trailing metadata.
          code: The RPC's status code.
          details: The RPC's status details.
        """
        raise NotImplementedError()


class StreamStreamChannelRpc(six.with_metaclass(abc.ABCMeta)):
    """Fixture for a stream-stream RPC invoked by a system under test.

    Enables users to "play server" for the RPC.
    """

    @abc.abstractmethod
    def send_initial_metadata(self, initial_metadata):
        """Sends the RPC's initial metadata to the system under test.

        Args:
          initial_metadata: The RPC's initial metadata to be "sent" to the
            system under test.
        """
        raise NotImplementedError()

    @abc.abstractmethod
    def take_request(self):
        """Draws one of the requests added to the RPC by the system under test.

        This method blocks until the system under test has added to the RPC
        the request to be returned.

        Successive calls to this method return requests in the same order in
        which the system under test added them to the RPC.

        Returns:
          A request message added to the RPC by the system under test.
        """
        raise NotImplementedError()

    @abc.abstractmethod
    def send_response(self, response):
        """Sends a response to the system under test.

        Args:
          response: A response messages to be "sent" to the system under test.
        """
        raise NotImplementedError()

    @abc.abstractmethod
    def requests_closed(self):
        """Blocks until the system under test has closed the request stream."""
        raise NotImplementedError()

    @abc.abstractmethod
    def cancelled(self):
        """Blocks until the system under test has cancelled the RPC."""
        raise NotImplementedError()

    @abc.abstractmethod
    def terminate(self, trailing_metadata, code, details):
        """Terminates the RPC.

        Args:
          trailing_metadata: The RPC's trailing metadata.
          code: The RPC's status code.
          details: The RPC's status details.
        """
        raise NotImplementedError()


class Channel(six.with_metaclass(abc.ABCMeta), grpc.Channel):
    """A grpc.Channel double with which to test a system that invokes RPCs."""

    @abc.abstractmethod
    def take_unary_unary(self, method_descriptor):
        """Draws an RPC currently being made by the system under test.

        If the given descriptor does not identify any RPC currently being made
        by the system under test, this method blocks until the system under
        test invokes such an RPC.

        Args:
          method_descriptor: A descriptor.MethodDescriptor describing a
            unary-unary RPC method.

        Returns:
          A (invocation_metadata, request, unary_unary_channel_rpc) tuple of
            the RPC's invocation metadata, its request, and a
            UnaryUnaryChannelRpc with which to "play server" for the RPC.
        """
        raise NotImplementedError()

    @abc.abstractmethod
    def take_unary_stream(self, method_descriptor):
        """Draws an RPC currently being made by the system under test.

        If the given descriptor does not identify any RPC currently being made
        by the system under test, this method blocks until the system under
        test invokes such an RPC.

        Args:
          method_descriptor: A descriptor.MethodDescriptor describing a
            unary-stream RPC method.

        Returns:
          A (invocation_metadata, request, unary_stream_channel_rpc) tuple of
            the RPC's invocation metadata, its request, and a
            UnaryStreamChannelRpc with which to "play server" for the RPC.
        """
        raise NotImplementedError()

    @abc.abstractmethod
    def take_stream_unary(self, method_descriptor):
        """Draws an RPC currently being made by the system under test.

        If the given descriptor does not identify any RPC currently being made
        by the system under test, this method blocks until the system under
        test invokes such an RPC.

        Args:
          method_descriptor: A descriptor.MethodDescriptor describing a
            stream-unary RPC method.

        Returns:
          A (invocation_metadata, stream_unary_channel_rpc) tuple of the RPC's
            invocation metadata and a StreamUnaryChannelRpc with which to "play
            server" for the RPC.
        """
        raise NotImplementedError()

    @abc.abstractmethod
    def take_stream_stream(self, method_descriptor):
        """Draws an RPC currently being made by the system under test.

        If the given descriptor does not identify any RPC currently being made
        by the system under test, this method blocks until the system under
        test invokes such an RPC.

        Args:
          method_descriptor: A descriptor.MethodDescriptor describing a
            stream-stream RPC method.

        Returns:
          A (invocation_metadata, stream_stream_channel_rpc) tuple of the RPC's
            invocation metadata and a StreamStreamChannelRpc with which to
            "play server" for the RPC.
        """
        raise NotImplementedError()


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


def channel(service_descriptors, time):
    """Creates a Channel for use in tests of a gRPC Python-using system.

    Args:
      service_descriptors: An iterable of descriptor.ServiceDescriptors
        describing the RPCs that will be made on the returned Channel by the
        system under test.
      time: A Time to be used for tests.

    Returns:
      A Channel for use in tests.
    """
    from grpc_testing import _channel
    return _channel.testing_channel(service_descriptors, time)
