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

import threading

from grpc._cython import cygrpc


class SimpleFuture(object):
    """A simple future mechanism."""

    def __init__(self, function, *args, **kwargs):

        def wrapped_function():
            try:
                self._result = function(*args, **kwargs)
            except Exception as error:
                self._error = error

        self._result = None
        self._error = None
        self._thread = threading.Thread(target=wrapped_function)
        self._thread.start()

    def result(self):
        """The resulting value of this future.

    Re-raises any exceptions.
    """
        self._thread.join()
        if self._error:
            # TODO(atash): re-raise exceptions in a way that preserves tracebacks
            raise self._error
        return self._result


class CompletionQueuePollFuture(SimpleFuture):

    def __init__(self, completion_queue, deadline):
        super(CompletionQueuePollFuture,
              self).__init__(lambda: completion_queue.poll(deadline))
