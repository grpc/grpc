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
"""Coverage across the Face layer's generic-to-dynamic range for invocation."""

import abc

import six

from grpc.framework.common import cardinality

_CARDINALITY_TO_GENERIC_BLOCKING_BEHAVIOR = {
    cardinality.Cardinality.UNARY_UNARY: 'blocking_unary_unary',
    cardinality.Cardinality.UNARY_STREAM: 'inline_unary_stream',
    cardinality.Cardinality.STREAM_UNARY: 'blocking_stream_unary',
    cardinality.Cardinality.STREAM_STREAM: 'inline_stream_stream',
}

_CARDINALITY_TO_GENERIC_FUTURE_BEHAVIOR = {
    cardinality.Cardinality.UNARY_UNARY: 'future_unary_unary',
    cardinality.Cardinality.UNARY_STREAM: 'inline_unary_stream',
    cardinality.Cardinality.STREAM_UNARY: 'future_stream_unary',
    cardinality.Cardinality.STREAM_STREAM: 'inline_stream_stream',
}

_CARDINALITY_TO_GENERIC_EVENT_BEHAVIOR = {
    cardinality.Cardinality.UNARY_UNARY: 'event_unary_unary',
    cardinality.Cardinality.UNARY_STREAM: 'event_unary_stream',
    cardinality.Cardinality.STREAM_UNARY: 'event_stream_unary',
    cardinality.Cardinality.STREAM_STREAM: 'event_stream_stream',
}

_CARDINALITY_TO_MULTI_CALLABLE_ATTRIBUTE = {
    cardinality.Cardinality.UNARY_UNARY: 'unary_unary',
    cardinality.Cardinality.UNARY_STREAM: 'unary_stream',
    cardinality.Cardinality.STREAM_UNARY: 'stream_unary',
    cardinality.Cardinality.STREAM_STREAM: 'stream_stream',
}


class Invoker(six.with_metaclass(abc.ABCMeta)):
    """A type used to invoke test RPCs."""

    @abc.abstractmethod
    def blocking(self, group, name):
        """Invokes an RPC with blocking control flow."""
        raise NotImplementedError()

    @abc.abstractmethod
    def future(self, group, name):
        """Invokes an RPC with future control flow."""
        raise NotImplementedError()

    @abc.abstractmethod
    def event(self, group, name):
        """Invokes an RPC with event control flow."""
        raise NotImplementedError()


class InvokerConstructor(six.with_metaclass(abc.ABCMeta)):
    """A type used to create Invokers."""

    @abc.abstractmethod
    def name(self):
        """Specifies the name of the Invoker constructed by this object."""
        raise NotImplementedError()

    @abc.abstractmethod
    def construct_invoker(self, generic_stub, dynamic_stubs, methods):
        """Constructs an Invoker for the given stubs and methods."""
        raise NotImplementedError()


class _GenericInvoker(Invoker):

    def __init__(self, generic_stub, methods):
        self._stub = generic_stub
        self._methods = methods

    def _behavior(self, group, name, cardinality_to_generic_method):
        method_cardinality = self._methods[group, name].cardinality()
        behavior = getattr(self._stub,
                           cardinality_to_generic_method[method_cardinality])
        return lambda *args, **kwargs: behavior(group, name, *args, **kwargs)

    def blocking(self, group, name):
        return self._behavior(group, name,
                              _CARDINALITY_TO_GENERIC_BLOCKING_BEHAVIOR)

    def future(self, group, name):
        return self._behavior(group, name,
                              _CARDINALITY_TO_GENERIC_FUTURE_BEHAVIOR)

    def event(self, group, name):
        return self._behavior(group, name,
                              _CARDINALITY_TO_GENERIC_EVENT_BEHAVIOR)


class _GenericInvokerConstructor(InvokerConstructor):

    def name(self):
        return 'GenericInvoker'

    def construct_invoker(self, generic_stub, dynamic_stub, methods):
        return _GenericInvoker(generic_stub, methods)


class _MultiCallableInvoker(Invoker):

    def __init__(self, generic_stub, methods):
        self._stub = generic_stub
        self._methods = methods

    def _multi_callable(self, group, name):
        method_cardinality = self._methods[group, name].cardinality()
        behavior = getattr(
            self._stub,
            _CARDINALITY_TO_MULTI_CALLABLE_ATTRIBUTE[method_cardinality])
        return behavior(group, name)

    def blocking(self, group, name):
        return self._multi_callable(group, name)

    def future(self, group, name):
        method_cardinality = self._methods[group, name].cardinality()
        behavior = getattr(
            self._stub,
            _CARDINALITY_TO_MULTI_CALLABLE_ATTRIBUTE[method_cardinality])
        if method_cardinality in (cardinality.Cardinality.UNARY_UNARY,
                                  cardinality.Cardinality.STREAM_UNARY):
            return behavior(group, name).future
        else:
            return behavior(group, name)

    def event(self, group, name):
        return self._multi_callable(group, name).event


class _MultiCallableInvokerConstructor(InvokerConstructor):

    def name(self):
        return 'MultiCallableInvoker'

    def construct_invoker(self, generic_stub, dynamic_stub, methods):
        return _MultiCallableInvoker(generic_stub, methods)


class _DynamicInvoker(Invoker):

    def __init__(self, dynamic_stubs, methods):
        self._stubs = dynamic_stubs
        self._methods = methods

    def blocking(self, group, name):
        return getattr(self._stubs[group], name)

    def future(self, group, name):
        if self._methods[group, name].cardinality() in (
                cardinality.Cardinality.UNARY_UNARY,
                cardinality.Cardinality.STREAM_UNARY):
            return getattr(self._stubs[group], name).future
        else:
            return getattr(self._stubs[group], name)

    def event(self, group, name):
        return getattr(self._stubs[group], name).event


class _DynamicInvokerConstructor(InvokerConstructor):

    def name(self):
        return 'DynamicInvoker'

    def construct_invoker(self, generic_stub, dynamic_stubs, methods):
        return _DynamicInvoker(dynamic_stubs, methods)


def invoker_constructors():
    """Creates a sequence of InvokerConstructors to use in tests of RPCs.

  Returns:
    A sequence of InvokerConstructors.
  """
    return (_GenericInvokerConstructor(), _MultiCallableInvokerConstructor(),
            _DynamicInvokerConstructor(),)
