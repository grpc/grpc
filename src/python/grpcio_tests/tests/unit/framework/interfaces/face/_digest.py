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
"""Code for making a service.TestService more amenable to use in tests."""

import collections
import threading

import six

# test_control, _service, and test_interfaces are referenced from specification
# in this module.
from grpc.framework.common import cardinality
from grpc.framework.common import style
from grpc.framework.foundation import stream
from grpc.framework.foundation import stream_util
from grpc.framework.interfaces.face import face
from tests.unit.framework.common import test_control  # pylint: disable=unused-import
from tests.unit.framework.interfaces.face import _service  # pylint: disable=unused-import
from tests.unit.framework.interfaces.face import test_interfaces  # pylint: disable=unused-import

_IDENTITY = lambda x: x


class TestServiceDigest(
        collections.namedtuple('TestServiceDigest', (
            'methods', 'inline_method_implementations',
            'event_method_implementations', 'multi_method_implementation',
            'unary_unary_messages_sequences', 'unary_stream_messages_sequences',
            'stream_unary_messages_sequences',
            'stream_stream_messages_sequences',))):
    """A transformation of a service.TestService.

  Attributes:
    methods: A dict from method group-name pair to test_interfaces.Method object
      describing the RPC methods that may be called during the test.
    inline_method_implementations: A dict from method group-name pair to
      face.MethodImplementation object to be used in tests of in-line calls to
      behaviors under test.
    event_method_implementations: A dict from method group-name pair to
      face.MethodImplementation object to be used in tests of event-driven calls
      to behaviors under test.
    multi_method_implementation: A face.MultiMethodImplementation to be used in
      tests of generic calls to behaviors under test.
    unary_unary_messages_sequences: A dict from method group-name pair to
      sequence of service.UnaryUnaryTestMessages objects to be used to test the
      identified method.
    unary_stream_messages_sequences: A dict from method group-name pair to
      sequence of service.UnaryStreamTestMessages objects to be used to test the
      identified method.
    stream_unary_messages_sequences: A dict from method group-name pair to
      sequence of service.StreamUnaryTestMessages objects to be used to test the
      identified method.
    stream_stream_messages_sequences: A dict from method group-name pair to
      sequence of service.StreamStreamTestMessages objects to be used to test
      the identified method.
  """


class _BufferingConsumer(stream.Consumer):
    """A trivial Consumer that dumps what it consumes in a user-mutable buffer."""

    def __init__(self):
        self.consumed = []
        self.terminated = False

    def consume(self, value):
        self.consumed.append(value)

    def terminate(self):
        self.terminated = True

    def consume_and_terminate(self, value):
        self.consumed.append(value)
        self.terminated = True


class _InlineUnaryUnaryMethod(face.MethodImplementation):

    def __init__(self, unary_unary_test_method, control):
        self._test_method = unary_unary_test_method
        self._control = control

        self.cardinality = cardinality.Cardinality.UNARY_UNARY
        self.style = style.Service.INLINE

    def unary_unary_inline(self, request, context):
        response_list = []
        self._test_method.service(request, response_list.append, context,
                                  self._control)
        return response_list.pop(0)


class _EventUnaryUnaryMethod(face.MethodImplementation):

    def __init__(self, unary_unary_test_method, control, pool):
        self._test_method = unary_unary_test_method
        self._control = control
        self._pool = pool

        self.cardinality = cardinality.Cardinality.UNARY_UNARY
        self.style = style.Service.EVENT

    def unary_unary_event(self, request, response_callback, context):
        if self._pool is None:
            self._test_method.service(request, response_callback, context,
                                      self._control)
        else:
            self._pool.submit(self._test_method.service, request,
                              response_callback, context, self._control)


class _InlineUnaryStreamMethod(face.MethodImplementation):

    def __init__(self, unary_stream_test_method, control):
        self._test_method = unary_stream_test_method
        self._control = control

        self.cardinality = cardinality.Cardinality.UNARY_STREAM
        self.style = style.Service.INLINE

    def unary_stream_inline(self, request, context):
        response_consumer = _BufferingConsumer()
        self._test_method.service(request, response_consumer, context,
                                  self._control)
        for response in response_consumer.consumed:
            yield response


class _EventUnaryStreamMethod(face.MethodImplementation):

    def __init__(self, unary_stream_test_method, control, pool):
        self._test_method = unary_stream_test_method
        self._control = control
        self._pool = pool

        self.cardinality = cardinality.Cardinality.UNARY_STREAM
        self.style = style.Service.EVENT

    def unary_stream_event(self, request, response_consumer, context):
        if self._pool is None:
            self._test_method.service(request, response_consumer, context,
                                      self._control)
        else:
            self._pool.submit(self._test_method.service, request,
                              response_consumer, context, self._control)


class _InlineStreamUnaryMethod(face.MethodImplementation):

    def __init__(self, stream_unary_test_method, control):
        self._test_method = stream_unary_test_method
        self._control = control

        self.cardinality = cardinality.Cardinality.STREAM_UNARY
        self.style = style.Service.INLINE

    def stream_unary_inline(self, request_iterator, context):
        response_list = []
        request_consumer = self._test_method.service(response_list.append,
                                                     context, self._control)
        for request in request_iterator:
            request_consumer.consume(request)
        request_consumer.terminate()
        return response_list.pop(0)


class _EventStreamUnaryMethod(face.MethodImplementation):

    def __init__(self, stream_unary_test_method, control, pool):
        self._test_method = stream_unary_test_method
        self._control = control
        self._pool = pool

        self.cardinality = cardinality.Cardinality.STREAM_UNARY
        self.style = style.Service.EVENT

    def stream_unary_event(self, response_callback, context):
        request_consumer = self._test_method.service(response_callback, context,
                                                     self._control)
        if self._pool is None:
            return request_consumer
        else:
            return stream_util.ThreadSwitchingConsumer(request_consumer,
                                                       self._pool)


class _InlineStreamStreamMethod(face.MethodImplementation):

    def __init__(self, stream_stream_test_method, control):
        self._test_method = stream_stream_test_method
        self._control = control

        self.cardinality = cardinality.Cardinality.STREAM_STREAM
        self.style = style.Service.INLINE

    def stream_stream_inline(self, request_iterator, context):
        response_consumer = _BufferingConsumer()
        request_consumer = self._test_method.service(response_consumer, context,
                                                     self._control)

        for request in request_iterator:
            request_consumer.consume(request)
            while response_consumer.consumed:
                yield response_consumer.consumed.pop(0)
        response_consumer.terminate()


class _EventStreamStreamMethod(face.MethodImplementation):

    def __init__(self, stream_stream_test_method, control, pool):
        self._test_method = stream_stream_test_method
        self._control = control
        self._pool = pool

        self.cardinality = cardinality.Cardinality.STREAM_STREAM
        self.style = style.Service.EVENT

    def stream_stream_event(self, response_consumer, context):
        request_consumer = self._test_method.service(response_consumer, context,
                                                     self._control)
        if self._pool is None:
            return request_consumer
        else:
            return stream_util.ThreadSwitchingConsumer(request_consumer,
                                                       self._pool)


class _UnaryConsumer(stream.Consumer):
    """A Consumer that only allows consumption of exactly one value."""

    def __init__(self, action):
        self._lock = threading.Lock()
        self._action = action
        self._consumed = False
        self._terminated = False

    def consume(self, value):
        with self._lock:
            if self._consumed:
                raise ValueError('Unary consumer already consumed!')
            elif self._terminated:
                raise ValueError('Unary consumer already terminated!')
            else:
                self._consumed = True

        self._action(value)

    def terminate(self):
        with self._lock:
            if not self._consumed:
                raise ValueError('Unary consumer hasn\'t yet consumed!')
            elif self._terminated:
                raise ValueError('Unary consumer already terminated!')
            else:
                self._terminated = True

    def consume_and_terminate(self, value):
        with self._lock:
            if self._consumed:
                raise ValueError('Unary consumer already consumed!')
            elif self._terminated:
                raise ValueError('Unary consumer already terminated!')
            else:
                self._consumed = True
                self._terminated = True

        self._action(value)


class _UnaryUnaryAdaptation(object):

    def __init__(self, unary_unary_test_method):
        self._method = unary_unary_test_method

    def service(self, response_consumer, context, control):

        def action(request):
            self._method.service(request,
                                 response_consumer.consume_and_terminate,
                                 context, control)

        return _UnaryConsumer(action)


class _UnaryStreamAdaptation(object):

    def __init__(self, unary_stream_test_method):
        self._method = unary_stream_test_method

    def service(self, response_consumer, context, control):

        def action(request):
            self._method.service(request, response_consumer, context, control)

        return _UnaryConsumer(action)


class _StreamUnaryAdaptation(object):

    def __init__(self, stream_unary_test_method):
        self._method = stream_unary_test_method

    def service(self, response_consumer, context, control):
        return self._method.service(response_consumer.consume_and_terminate,
                                    context, control)


class _MultiMethodImplementation(face.MultiMethodImplementation):

    def __init__(self, methods, control, pool):
        self._methods = methods
        self._control = control
        self._pool = pool

    def service(self, group, name, response_consumer, context):
        method = self._methods.get(group, name, None)
        if method is None:
            raise face.NoSuchMethodError(group, name)
        elif self._pool is None:
            return method(response_consumer, context, self._control)
        else:
            request_consumer = method(response_consumer, context, self._control)
            return stream_util.ThreadSwitchingConsumer(request_consumer,
                                                       self._pool)


class _Assembly(
        collections.namedtuple(
            '_Assembly',
            ['methods', 'inlines', 'events', 'adaptations', 'messages'])):
    """An intermediate structure created when creating a TestServiceDigest."""


def _assemble(scenarios, identifiers, inline_method_constructor,
              event_method_constructor, adapter, control, pool):
    """Creates an _Assembly from the given scenarios."""
    methods = {}
    inlines = {}
    events = {}
    adaptations = {}
    messages = {}
    for identifier, scenario in six.iteritems(scenarios):
        if identifier in identifiers:
            raise ValueError('Repeated identifier "(%s, %s)"!' % identifier)

        test_method = scenario[0]
        inline_method = inline_method_constructor(test_method, control)
        event_method = event_method_constructor(test_method, control, pool)
        adaptation = adapter(test_method)

        methods[identifier] = test_method
        inlines[identifier] = inline_method
        events[identifier] = event_method
        adaptations[identifier] = adaptation
        messages[identifier] = scenario[1]

    return _Assembly(methods, inlines, events, adaptations, messages)


def digest(service, control, pool):
    """Creates a TestServiceDigest from a TestService.

  Args:
    service: A _service.TestService.
    control: A test_control.Control.
    pool: If RPC methods should be serviced in a separate thread, a thread pool.
      None if RPC methods should be serviced in the thread belonging to the
      run-time that calls for their service.

  Returns:
    A TestServiceDigest synthesized from the given service.TestService.
  """
    identifiers = set()

    unary_unary = _assemble(service.unary_unary_scenarios(), identifiers,
                            _InlineUnaryUnaryMethod, _EventUnaryUnaryMethod,
                            _UnaryUnaryAdaptation, control, pool)
    identifiers.update(unary_unary.inlines)

    unary_stream = _assemble(service.unary_stream_scenarios(), identifiers,
                             _InlineUnaryStreamMethod, _EventUnaryStreamMethod,
                             _UnaryStreamAdaptation, control, pool)
    identifiers.update(unary_stream.inlines)

    stream_unary = _assemble(service.stream_unary_scenarios(), identifiers,
                             _InlineStreamUnaryMethod, _EventStreamUnaryMethod,
                             _StreamUnaryAdaptation, control, pool)
    identifiers.update(stream_unary.inlines)

    stream_stream = _assemble(service.stream_stream_scenarios(), identifiers,
                              _InlineStreamStreamMethod,
                              _EventStreamStreamMethod, _IDENTITY, control,
                              pool)
    identifiers.update(stream_stream.inlines)

    methods = dict(unary_unary.methods)
    methods.update(unary_stream.methods)
    methods.update(stream_unary.methods)
    methods.update(stream_stream.methods)
    adaptations = dict(unary_unary.adaptations)
    adaptations.update(unary_stream.adaptations)
    adaptations.update(stream_unary.adaptations)
    adaptations.update(stream_stream.adaptations)
    inlines = dict(unary_unary.inlines)
    inlines.update(unary_stream.inlines)
    inlines.update(stream_unary.inlines)
    inlines.update(stream_stream.inlines)
    events = dict(unary_unary.events)
    events.update(unary_stream.events)
    events.update(stream_unary.events)
    events.update(stream_stream.events)

    return TestServiceDigest(
        methods, inlines, events,
        _MultiMethodImplementation(adaptations, control, pool),
        unary_unary.messages, unary_stream.messages, stream_unary.messages,
        stream_stream.messages)
