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
"""Helpful utilities related to the stream module."""

import logging
import threading
from typing import Callable, Iterator, Protocol, TypeVar, Union

from grpc.framework.foundation import stream

_NO_VALUE = object()
_LOGGER = logging.getLogger(__name__)

T = TypeVar('T')
U = TypeVar('U')


class ThreadPoolProtocol(Protocol):
    """Protocol for thread pool objects."""
    
    def submit(self, fn: Callable[..., T], *args, **kwargs) -> T:
        ...


class TransformingConsumer(stream.Consumer[T]):
    """A stream.Consumer that passes a transformation of its input to another."""

    def __init__(self, transformation: Callable[[T], U], downstream: stream.Consumer[U]) -> None:
        self._transformation = transformation
        self._downstream = downstream

    def consume(self, value: T) -> None:
        self._downstream.consume(self._transformation(value))

    def terminate(self) -> None:
        self._downstream.terminate()

    def consume_and_terminate(self, value: T) -> None:
        self._downstream.consume_and_terminate(self._transformation(value))


class IterableConsumer(stream.Consumer[T]):
    """A Consumer that when iterated over emits the values it has consumed."""

    def __init__(self) -> None:
        self._condition = threading.Condition()
        self._values: list[T] = []
        self._active = True

    def consume(self, value: T) -> None:
        with self._condition:
            if self._active:
                self._values.append(value)
                self._condition.notify()

    def terminate(self) -> None:
        with self._condition:
            self._active = False
            self._condition.notify()

    def consume_and_terminate(self, value: T) -> None:
        with self._condition:
            if self._active:
                self._values.append(value)
                self._active = False
                self._condition.notify()

    def __iter__(self) -> Iterator[T]:
        return self

    def __next__(self) -> T:
        return self.next()

    def next(self) -> T:
        with self._condition:
            while self._active and not self._values:
                self._condition.wait()
            if self._values:
                return self._values.pop(0)
            else:
                raise StopIteration()


class ThreadSwitchingConsumer(stream.Consumer[T]):
    """A Consumer decorator that affords serialization and asynchrony."""

    def __init__(self, sink: stream.Consumer[T], pool: ThreadPoolProtocol) -> None:
        self._lock = threading.Lock()
        self._sink = sink
        self._pool = pool
        # True if self._spin has been submitted to the pool to be called once and
        # that call has not yet returned, False otherwise.
        self._spinning = False
        self._values: list[T] = []
        self._active = True

    def _spin(self, sink: stream.Consumer[T], value: Union[T, object], terminate: bool) -> None:
        while True:
            try:
                if value is _NO_VALUE:
                    sink.terminate()
                elif terminate:
                    sink.consume_and_terminate(value)  # type: ignore
                else:
                    sink.consume(value)  # type: ignore
            except Exception as e:  # pylint:disable=broad-except
                _LOGGER.exception(e)

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

    def consume(self, value: T) -> None:
        with self._lock:
            if self._active:
                if self._spinning:
                    self._values.append(value)
                else:
                    self._pool.submit(self._spin, self._sink, value, False)
                    self._spinning = True

    def terminate(self) -> None:
        with self._lock:
            if self._active:
                self._active = False
                if not self._spinning:
                    self._pool.submit(self._spin, self._sink, _NO_VALUE, True)
                    self._spinning = True

    def consume_and_terminate(self, value: T) -> None:
        with self._lock:
            if self._active:
                self._active = False
                if self._spinning:
                    self._values.append(value)
                else:
                    self._pool.submit(self._spin, self._sink, value, True)
                    self._spinning = True
