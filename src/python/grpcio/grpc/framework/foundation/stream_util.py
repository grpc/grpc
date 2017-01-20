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
"""Helpful utilities related to the stream module."""

import logging
import threading

from grpc.framework.foundation import stream

_NO_VALUE = object()


class TransformingConsumer(stream.Consumer):
    """A stream.Consumer that passes a transformation of its input to another."""

    def __init__(self, transformation, downstream):
        self._transformation = transformation
        self._downstream = downstream

    def consume(self, value):
        self._downstream.consume(self._transformation(value))

    def terminate(self):
        self._downstream.terminate()

    def consume_and_terminate(self, value):
        self._downstream.consume_and_terminate(self._transformation(value))


class IterableConsumer(stream.Consumer):
    """A Consumer that when iterated over emits the values it has consumed."""

    def __init__(self):
        self._condition = threading.Condition()
        self._values = []
        self._active = True

    def consume(self, stock_reply):
        with self._condition:
            if self._active:
                self._values.append(stock_reply)
                self._condition.notify()

    def terminate(self):
        with self._condition:
            self._active = False
            self._condition.notify()

    def consume_and_terminate(self, stock_reply):
        with self._condition:
            if self._active:
                self._values.append(stock_reply)
                self._active = False
                self._condition.notify()

    def __iter__(self):
        return self

    def __next__(self):
        return self.next()

    def next(self):
        with self._condition:
            while self._active and not self._values:
                self._condition.wait()
            if self._values:
                return self._values.pop(0)
            else:
                raise StopIteration()


class ThreadSwitchingConsumer(stream.Consumer):
    """A Consumer decorator that affords serialization and asynchrony."""

    def __init__(self, sink, pool):
        self._lock = threading.Lock()
        self._sink = sink
        self._pool = pool
        # True if self._spin has been submitted to the pool to be called once and
        # that call has not yet returned, False otherwise.
        self._spinning = False
        self._values = []
        self._active = True

    def _spin(self, sink, value, terminate):
        while True:
            try:
                if value is _NO_VALUE:
                    sink.terminate()
                elif terminate:
                    sink.consume_and_terminate(value)
                else:
                    sink.consume(value)
            except Exception as e:  # pylint:disable=broad-except
                logging.exception(e)

            with self._lock:
                if terminate:
                    self._spinning = False
                    return
                elif self._values:
                    value = self._values.pop(0)
                    terminate = not self._values and not self._active
                elif not self._active:
                    value = _NO_VALUE
                    terminate = True
                else:
                    self._spinning = False
                    return

    def consume(self, value):
        with self._lock:
            if self._active:
                if self._spinning:
                    self._values.append(value)
                else:
                    self._pool.submit(self._spin, self._sink, value, False)
                    self._spinning = True

    def terminate(self):
        with self._lock:
            if self._active:
                self._active = False
                if not self._spinning:
                    self._pool.submit(self._spin, self._sink, _NO_VALUE, True)
                    self._spinning = True

    def consume_and_terminate(self, value):
        with self._lock:
            if self._active:
                self._active = False
                if self._spinning:
                    self._values.append(value)
                else:
                    self._pool.submit(self._spin, self._sink, value, True)
                    self._spinning = True
