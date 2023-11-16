# Copyright 2019 The gRPC Authors
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

import asyncio
import functools
import logging
from typing import Callable
import unittest

from grpc.experimental import aio

__all__ = "AioTestBase"

_COROUTINE_FUNCTION_ALLOWLIST = ["setUp", "tearDown"]


def _async_to_sync_decorator(f: Callable, loop: asyncio.AbstractEventLoop):
    @functools.wraps(f)
    def wrapper(*args, **kwargs):
        return loop.run_until_complete(f(*args, **kwargs))

    return wrapper


def _get_default_loop(debug=True):
    try:
        loop = asyncio.get_event_loop()
    except:
        loop = asyncio.new_event_loop()
        asyncio.set_event_loop(loop)
    finally:
        loop.set_debug(debug)
        return loop


# NOTE(gnossen) this test class can also be implemented with metaclass.
class AioTestBase(unittest.TestCase):
    # NOTE(lidi) We need to pick a loop for entire testing phase, otherwise it
    # will trigger create new loops in new threads, leads to deadlock.
    _TEST_LOOP = _get_default_loop()

    @property
    def loop(self):
        return self._TEST_LOOP

    def __getattribute__(self, name):
        """Overrides the loading logic to support coroutine functions."""
        attr = super().__getattribute__(name)

        # If possible, converts the coroutine into a sync function.
        if name.startswith("test_") or name in _COROUTINE_FUNCTION_ALLOWLIST:
            if asyncio.iscoroutinefunction(attr):
                return _async_to_sync_decorator(attr, self._TEST_LOOP)
        # For other attributes, let them pass.
        return attr
