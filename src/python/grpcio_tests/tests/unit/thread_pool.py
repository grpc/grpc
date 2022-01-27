# Copyright 2016 gRPC authors.
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

from concurrent import futures
import threading


class RecordingThreadPool(futures.ThreadPoolExecutor):
    """A thread pool that records if used."""

    def __init__(self, max_workers):
        self._tp_executor = futures.ThreadPoolExecutor(max_workers=max_workers)
        self._lock = threading.Lock()
        self._was_used = False

    def submit(self, fn, *args, **kwargs):  # pylint: disable=arguments-differ
        with self._lock:
            self._was_used = True
        self._tp_executor.submit(fn, *args, **kwargs)

    def was_used(self):
        with self._lock:
            return self._was_used
