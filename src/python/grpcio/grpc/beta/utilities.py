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
"""Utilities for the gRPC Python Beta API."""

import threading
import time

# implementations is referenced from specification in this module.
from grpc.beta import implementations  # pylint: disable=unused-import
from grpc.beta import interfaces
from grpc.framework.foundation import callable_util
from grpc.framework.foundation import future

_DONE_CALLBACK_EXCEPTION_LOG_MESSAGE = (
    'Exception calling connectivity future "done" callback!')


class _ChannelReadyFuture(future.Future):

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
                    raise future.CancelledError()
                elif self._matured:
                    return
                else:
                    if until is None:
                        self._condition.wait()
                    else:
                        remaining = until - time.time()
                        if remaining < 0:
                            raise future.TimeoutError()
                        else:
                            self._condition.wait(timeout=remaining)

    def _update(self, connectivity):
        with self._condition:
            if (not self._cancelled and
                    connectivity is interfaces.ChannelConnectivity.READY):
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
    """Creates a future.Future tracking when an implementations.Channel is ready.

  Cancelling the returned future.Future does not tell the given
  implementations.Channel to abandon attempts it may have been making to
  connect; cancelling merely deactivates the return future.Future's
  subscription to the given implementations.Channel's connectivity.

  Args:
    channel: An implementations.Channel.

  Returns:
    A future.Future that matures when the given Channel has connectivity
      interfaces.ChannelConnectivity.READY.
  """
    ready_future = _ChannelReadyFuture(channel)
    ready_future.start()
    return ready_future
