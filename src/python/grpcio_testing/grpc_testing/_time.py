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
"""Test times."""

import collections
import logging
import threading
import time as _time
import types
from typing import Any, Callable, Optional, Sequence

import grpc
import grpc_testing

logging.basicConfig()
_LOGGER = logging.getLogger(__name__)


def _call(behaviors: Callable[[], Any]):
    for behavior in behaviors:
        try:
            behavior()
        except Exception:  # pylint: disable=broad-except
            _LOGGER.exception('Exception calling behavior "%r"!', behavior)


def _call_in_thread(behaviors: Sequence[Optional[Callable[[], Any]]]) -> None:
    calling = threading.Thread(target=_call, args=(behaviors,))
    calling.start()
    # NOTE(nathaniel): Because this function is called from "strict" Time
    # implementations, it blocks until after all behaviors have terminated.
    calling.join()


class _State(object):

    def __init__(self):
        self.condition = threading.Condition()
        self.times_to_behaviors = collections.defaultdict(list)


class _Delta(
        collections.namedtuple('_Delta', (
            'mature_behaviors',
            'earliest_mature_time',
            'earliest_immature_time',
        ))):
    pass


def _process(state: _State, now: float) -> _Delta:
    mature_behaviors = []
    earliest_mature_time = None
    while state.times_to_behaviors:
        earliest_time = min(state.times_to_behaviors)
        if earliest_time <= now:
            if earliest_mature_time is None:
                earliest_mature_time = earliest_time
            earliest_mature_behaviors = state.times_to_behaviors.pop(
                earliest_time)
            mature_behaviors.extend(earliest_mature_behaviors)
        else:
            earliest_immature_time = earliest_time
            break
    else:
        earliest_immature_time = None
    return _Delta(mature_behaviors, earliest_mature_time,
                  earliest_immature_time)


class _Future(grpc.Future):
    _state: _State
    _behavior: Callable[[], Any]
    _time: float
    _cancelled: bool

    def __init__(self, state: _State, behavior: Callable[[], Any], time: float):
        self._state = state
        self._behavior = behavior
        self._time = time
        self._cancelled = False

    def cancel(self) -> bool:
        with self._state.condition:
            if self._cancelled:
                return True
            else:
                behaviors_at_time = self._state.times_to_behaviors.get(
                    self._time)
                if behaviors_at_time is None:
                    return False
                else:
                    behaviors_at_time.remove(self._behavior)
                    if not behaviors_at_time:
                        self._state.times_to_behaviors.pop(self._time)
                        self._state.condition.notify_all()
                    self._cancelled = True
                    return True

    def cancelled(self) -> bool:
        with self._state.condition:
            return self._cancelled

    def running(self) -> bool:
        raise NotImplementedError()

    def done(self) -> bool:
        raise NotImplementedError()

    def result(self, timeout: Optional[float] = None) -> Any:
        raise NotImplementedError()

    def exception(self, timeout: Optional[float] = None) -> Optional[Exception]:
        raise NotImplementedError()

    def traceback(
            self,
            timeout: Optional[float] = None) -> Optional[types.TracebackType]:
        raise NotImplementedError()

    def add_done_callback(self, fn: Callable[[grpc.Future], None]) -> None:
        raise NotImplementedError()


class StrictRealTime(grpc_testing.Time):
    _state: _State
    _active: bool
    _caling: Optional[float]

    def __init__(self):
        self._state = _State()
        self._active = False
        self._calling = None

    def _activity(self) -> None:
        while True:
            with self._state.condition:
                while True:
                    now = _time.time()
                    delta = _process(self._state, now)
                    self._state.condition.notify_all()
                    if delta.mature_behaviors:
                        self._calling = delta.earliest_mature_time
                        break
                    self._calling = None
                    if delta.earliest_immature_time is None:
                        self._active = False
                        return
                    else:
                        timeout = max(0, delta.earliest_immature_time - now)
                        self._state.condition.wait(timeout=timeout)
            _call(delta.mature_behaviors)

    def _ensure_called_through(self, time: float) -> None:
        with self._state.condition:
            while ((self._state.times_to_behaviors and
                    min(self._state.times_to_behaviors) < time) or
                   (self._calling is not None and self._calling < time)):
                self._state.condition.wait()

    def _call_at(self, behavior: Callable[[], Any], time: float) -> _Future:
        with self._state.condition:
            self._state.times_to_behaviors[time].append(behavior)
            if self._active:
                self._state.condition.notify_all()
            else:
                activity = threading.Thread(target=self._activity)
                activity.start()
                self._active = True
            return _Future(self._state, behavior, time)

    def time(self) -> float:
        return _time.time()

    def call_in(self, behavior: Callable[[], Any], delay: float) -> _Future:
        return self._call_at(behavior, _time.time() + delay)

    def call_at(self, behavior: Callable[[], Any], time: float) -> _Future:
        return self._call_at(behavior, time)

    def sleep_for(self, duration: float) -> None:
        time = _time.time() + duration
        _time.sleep(duration)
        self._ensure_called_through(time)

    def sleep_until(self, time: float) -> None:
        _time.sleep(max(0, time - _time.time()))
        self._ensure_called_through(time)


class StrictFakeTime(grpc_testing.Time):
    _state: _State
    _time: float

    def __init__(self, time: float):
        self._state = _State()
        self._time = time

    def time(self) -> float:
        return self._time

    def call_in(self, behavior: Callable[[], Any],
                delay: Optional[float]) -> _Future:
        if delay <= 0.0:
            _call_in_thread((behavior,))
        else:
            with self._state.condition:
                time = self._time + delay
                self._state.times_to_behaviors[time].append(behavior)
        return _Future(self._state, behavior, time)

    def call_at(self, behavior: Callable[[], Any],
                time: Optional[float]) -> _Future:
        with self._state.condition:
            if time <= self._time:
                _call_in_thread((behavior,))
            else:
                self._state.times_to_behaviors[time].append(behavior)
        return _Future(self._state, behavior, time)

    def sleep_for(self, duration: float) -> None:
        if 0.0 < duration:
            with self._state.condition:
                self._time += duration
                delta = _process(self._state, self._time)
                _call_in_thread(delta.mature_behaviors)

    def sleep_until(self, time: float) -> None:
        with self._state.condition:
            if self._time < time:
                self._time = time
                delta = _process(self._state, self._time)
                _call_in_thread(delta.mature_behaviors)
