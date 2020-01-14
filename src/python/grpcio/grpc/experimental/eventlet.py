# Copyright 2020 gRPC authors.
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
"""gRPC's Python Eventlet APIs."""

import sys
from grpc._cython import cygrpc as _cygrpc


def init_eventlet(connection_backlog=50):
    """Patches gRPC's libraries to be compatible with eventlet.

    This must be called AFTER the python standard lib has been patched,
    but BEFORE creating and gRPC objects::

        import eventlet
        eventlet.monkey_patch()  # noqa

        import grpc
        from grpc.experimental import eventlet as grpc_eventlet

        if __name__ == '__main__':
            grpc_eventlet.init_eventlet()

    For the gRPC library to operate the application must not block eventlet's
    MAINLOOP, because the asynchronous calls in the library are scheduled in
    the same hub.

    Args:
        connection_backlog (int): Number of unaccepted connections that the
            system will allow before refusing new connections. Defaults to 50.
    """
    # Fix deadlock from issue https://github.com/eventlet/eventlet/issues/508
    if sys.version_info >= (3, 7):
        import queue
        queue.SimpleQueue = queue._PySimpleQueue

    _cygrpc.init_grpc_eventlet(connection_backlog)
