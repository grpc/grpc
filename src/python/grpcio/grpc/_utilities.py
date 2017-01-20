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
"""Internal utilities for gRPC Python."""

import collections
import threading
import time

import six

import grpc
from grpc import _common
from grpc.framework.foundation import callable_util

_DONE_CALLBACK_EXCEPTION_LOG_MESSAGE = (
    'Exception calling connectivity future "done" callback!')


class RpcMethodHandler(
        collections.namedtuple('_RpcMethodHandler', (
            'request_streaming',
            'response_streaming',
            'request_deserializer',
            'response_serializer',
            'unary_unary',
            'unary_stream',
            'stream_unary',
            'stream_stream',)), grpc.RpcMethodHandler):
    pass


class DictionaryGenericHandler(grpc.ServiceRpcHandler):

    def __init__(self, service, method_handlers):
        self._name = service
        self._method_handlers = {
            _common.fully_qualified_method(service, method): method_handler
            for method, method_handler in six.iteritems(method_handlers)
        }

    def service_name(self):
        return self._name

    def service(self, handler_call_details):
        return self._method_handlers.get(handler_call_details.method)


class _ChannelReadyFuture(grpc.Future):

    def __init__(self, channel):
        self._condition = threading.Condition()
        self._channel = channel

        self._matured = False
        self._cancelled = False
        self._done_callbacks = []

    def _block(self, timeout):
        until = None if timeout is None else time.time() + timeout
        with self._condition:
            while True:
                if self._cancelled:
                    raise grpc.FutureCancelledError()
                elif self._matured:
                    return
                else:
                    if until is None:
                        self._condition.wait()
                    else:
                        remaining = until - time.time()
                        if remaining < 0:
                            raise grpc.FutureTimeoutError()
                        else:
                            self._condition.wait(timeout=remaining)

    def _update(self, connectivity):
        with self._condition:
            if (not self._cancelled and
                    connectivity is grpc.ChannelConnectivity.READY):
                self._matured = True
                self._channel.unsubscribe(self._update)
                self._condition.notify_all()
                done_callbacks = tuple(self._done_callbacks)
                self._done_callbacks = None
            else:
                return

        for done_callback in done_callbacks:
            callable_util.call_logging_exceptions(
                done_callback, _DONE_CALLBACK_EXCEPTION_LOG_MESSAGE, self)

    def cancel(self):
        with self._condition:
            if not self._matured:
                self._cancelled = True
                self._channel.unsubscribe(self._update)
                self._condition.notify_all()
                done_callbacks = tuple(self._done_callbacks)
                self._done_callbacks = None
            else:
                return False

        for done_callback in done_callbacks:
            callable_util.call_logging_exceptions(
                done_callback, _DONE_CALLBACK_EXCEPTION_LOG_MESSAGE, self)

    def cancelled(self):
        with self._condition:
            return self._cancelled

    def running(self):
        with self._condition:
            return not self._cancelled and not self._matured

    def done(self):
        with self._condition:
            return self._cancelled or self._matured

    def result(self, timeout=None):
        self._block(timeout)
        return None

    def exception(self, timeout=None):
        self._block(timeout)
        return None

    def traceback(self, timeout=None):
        self._block(timeout)
        return None

    def add_done_callback(self, fn):
        with self._condition:
            if not self._cancelled and not self._matured:
                self._done_callbacks.append(fn)
                return

        fn(self)

    def start(self):
        with self._condition:
            self._channel.subscribe(self._update, try_to_connect=True)

    def __del__(self):
        with self._condition:
            if not self._cancelled and not self._matured:
                self._channel.unsubscribe(self._update)


def channel_ready_future(channel):
    ready_future = _ChannelReadyFuture(channel)
    ready_future.start()
    return ready_future
