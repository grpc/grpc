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
"""Private interfaces implemented by data sets used in Face-layer tests."""

import abc

import six

# face is referenced from specification in this module.
from grpc.framework.interfaces.face import face  # pylint: disable=unused-import
from tests.unit.framework.interfaces.face import test_interfaces


class UnaryUnaryTestMethodImplementation(
        six.with_metaclass(abc.ABCMeta, test_interfaces.Method)):
    """A controllable implementation of a unary-unary method."""

    @abc.abstractmethod
    def service(self, request, response_callback, context, control):
        """Services an RPC that accepts one message and produces one message.

    Args:
      request: The single request message for the RPC.
      response_callback: A callback to be called to accept the response message
        of the RPC.
      context: An face.ServicerContext object.
      control: A test_control.Control to control execution of this method.

    Raises:
      abandonment.Abandoned: May or may not be raised when the RPC has been
        aborted.
    """
        raise NotImplementedError()


class UnaryUnaryTestMessages(six.with_metaclass(abc.ABCMeta)):
    """A type for unary-request-unary-response message pairings."""

    @abc.abstractmethod
    def request(self):
        """Affords a request message.

    Implementations of this method should return a different message with each
    call so that multiple test executions of the test method may be made with
    different inputs.

    Returns:
      A request message.
    """
        raise NotImplementedError()

    @abc.abstractmethod
    def verify(self, request, response, test_case):
        """Verifies that the computed response matches the given request.

    Args:
      request: A request message.
      response: A response message.
      test_case: A unittest.TestCase object affording useful assertion methods.

    Raises:
      AssertionError: If the request and response do not match, indicating that
        there was some problem executing the RPC under test.
    """
        raise NotImplementedError()


class UnaryStreamTestMethodImplementation(
        six.with_metaclass(abc.ABCMeta, test_interfaces.Method)):
    """A controllable implementation of a unary-stream method."""

    @abc.abstractmethod
    def service(self, request, response_consumer, context, control):
        """Services an RPC that takes one message and produces a stream of messages.

    Args:
      request: The single request message for the RPC.
      response_consumer: A stream.Consumer to be called to accept the response
        messages of the RPC.
      context: A face.ServicerContext object.
      control: A test_control.Control to control execution of this method.

    Raises:
      abandonment.Abandoned: May or may not be raised when the RPC has been
        aborted.
    """
        raise NotImplementedError()


class UnaryStreamTestMessages(six.with_metaclass(abc.ABCMeta)):
    """A type for unary-request-stream-response message pairings."""

    @abc.abstractmethod
    def request(self):
        """Affords a request message.

    Implementations of this method should return a different message with each
    call so that multiple test executions of the test method may be made with
    different inputs.

    Returns:
      A request message.
    """
        raise NotImplementedError()

    @abc.abstractmethod
    def verify(self, request, responses, test_case):
        """Verifies that the computed responses match the given request.

    Args:
      request: A request message.
      responses: A sequence of response messages.
      test_case: A unittest.TestCase object affording useful assertion methods.

    Raises:
      AssertionError: If the request and responses do not match, indicating that
        there was some problem executing the RPC under test.
    """
        raise NotImplementedError()


class StreamUnaryTestMethodImplementation(
        six.with_metaclass(abc.ABCMeta, test_interfaces.Method)):
    """A controllable implementation of a stream-unary method."""

    @abc.abstractmethod
    def service(self, response_callback, context, control):
        """Services an RPC that takes a stream of messages and produces one message.

    Args:
      response_callback: A callback to be called to accept the response message
        of the RPC.
      context: A face.ServicerContext object.
      control: A test_control.Control to control execution of this method.

    Returns:
      A stream.Consumer with which to accept the request messages of the RPC.
        The consumer returned from this method may or may not be invoked to
        completion: in the case of RPC abortion, RPC Framework will simply stop
        passing messages to this object. Implementations must not assume that
        this object will be called to completion of the request stream or even
        called at all.

    Raises:
      abandonment.Abandoned: May or may not be raised when the RPC has been
        aborted.
    """
        raise NotImplementedError()


class StreamUnaryTestMessages(six.with_metaclass(abc.ABCMeta)):
    """A type for stream-request-unary-response message pairings."""

    @abc.abstractmethod
    def requests(self):
        """Affords a sequence of request messages.

    Implementations of this method should return a different sequences with each
    call so that multiple test executions of the test method may be made with
    different inputs.

    Returns:
      A sequence of request messages.
    """
        raise NotImplementedError()

    @abc.abstractmethod
    def verify(self, requests, response, test_case):
        """Verifies that the computed response matches the given requests.

    Args:
      requests: A sequence of request messages.
      response: A response message.
      test_case: A unittest.TestCase object affording useful assertion methods.

    Raises:
      AssertionError: If the requests and response do not match, indicating that
        there was some problem executing the RPC under test.
    """
        raise NotImplementedError()


class StreamStreamTestMethodImplementation(
        six.with_metaclass(abc.ABCMeta, test_interfaces.Method)):
    """A controllable implementation of a stream-stream method."""

    @abc.abstractmethod
    def service(self, response_consumer, context, control):
        """Services an RPC that accepts and produces streams of messages.

    Args:
      response_consumer: A stream.Consumer to be called to accept the response
        messages of the RPC.
      context: A face.ServicerContext object.
      control: A test_control.Control to control execution of this method.

    Returns:
      A stream.Consumer with which to accept the request messages of the RPC.
        The consumer returned from this method may or may not be invoked to
        completion: in the case of RPC abortion, RPC Framework will simply stop
        passing messages to this object. Implementations must not assume that
        this object will be called to completion of the request stream or even
        called at all.

    Raises:
      abandonment.Abandoned: May or may not be raised when the RPC has been
        aborted.
    """
        raise NotImplementedError()


class StreamStreamTestMessages(six.with_metaclass(abc.ABCMeta)):
    """A type for stream-request-stream-response message pairings."""

    @abc.abstractmethod
    def requests(self):
        """Affords a sequence of request messages.

    Implementations of this method should return a different sequences with each
    call so that multiple test executions of the test method may be made with
    different inputs.

    Returns:
      A sequence of request messages.
    """
        raise NotImplementedError()

    @abc.abstractmethod
    def verify(self, requests, responses, test_case):
        """Verifies that the computed response matches the given requests.

    Args:
      requests: A sequence of request messages.
      responses: A sequence of response messages.
      test_case: A unittest.TestCase object affording useful assertion methods.

    Raises:
      AssertionError: If the requests and responses do not match, indicating
        that there was some problem executing the RPC under test.
    """
        raise NotImplementedError()


class TestService(six.with_metaclass(abc.ABCMeta)):
    """A specification of implemented methods to use in tests."""

    @abc.abstractmethod
    def unary_unary_scenarios(self):
        """Affords unary-request-unary-response test methods and their messages.

    Returns:
      A dict from method group-name pair to implementation/messages pair. The
        first element of the pair is a UnaryUnaryTestMethodImplementation object
        and the second element is a sequence of UnaryUnaryTestMethodMessages
        objects.
    """
        raise NotImplementedError()

    @abc.abstractmethod
    def unary_stream_scenarios(self):
        """Affords unary-request-stream-response test methods and their messages.

    Returns:
      A dict from method group-name pair to implementation/messages pair. The
        first element of the pair is a UnaryStreamTestMethodImplementation
        object and the second element is a sequence of
        UnaryStreamTestMethodMessages objects.
    """
        raise NotImplementedError()

    @abc.abstractmethod
    def stream_unary_scenarios(self):
        """Affords stream-request-unary-response test methods and their messages.

    Returns:
      A dict from method group-name pair to implementation/messages pair. The
        first element of the pair is a StreamUnaryTestMethodImplementation
        object and the second element is a sequence of
        StreamUnaryTestMethodMessages objects.
    """
        raise NotImplementedError()

    @abc.abstractmethod
    def stream_stream_scenarios(self):
        """Affords stream-request-stream-response test methods and their messages.

    Returns:
      A dict from method group-name pair to implementation/messages pair. The
        first element of the pair is a StreamStreamTestMethodImplementation
        object and the second element is a sequence of
        StreamStreamTestMethodMessages objects.
    """
        raise NotImplementedError()
