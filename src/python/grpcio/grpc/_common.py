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
"""Shared implementation."""

import logging
import threading
import time

import six

import grpc
from grpc._cython import cygrpc

CYGRPC_CONNECTIVITY_STATE_TO_CHANNEL_CONNECTIVITY = {
    cygrpc.ConnectivityState.idle:
    grpc.ChannelConnectivity.IDLE,
    cygrpc.ConnectivityState.connecting:
    grpc.ChannelConnectivity.CONNECTING,
    cygrpc.ConnectivityState.ready:
    grpc.ChannelConnectivity.READY,
    cygrpc.ConnectivityState.transient_failure:
    grpc.ChannelConnectivity.TRANSIENT_FAILURE,
    cygrpc.ConnectivityState.shutdown:
    grpc.ChannelConnectivity.SHUTDOWN,
}

CYGRPC_STATUS_CODE_TO_STATUS_CODE = {
    cygrpc.StatusCode.ok: grpc.StatusCode.OK,
    cygrpc.StatusCode.cancelled: grpc.StatusCode.CANCELLED,
    cygrpc.StatusCode.unknown: grpc.StatusCode.UNKNOWN,
    cygrpc.StatusCode.invalid_argument: grpc.StatusCode.INVALID_ARGUMENT,
    cygrpc.StatusCode.deadline_exceeded: grpc.StatusCode.DEADLINE_EXCEEDED,
    cygrpc.StatusCode.not_found: grpc.StatusCode.NOT_FOUND,
    cygrpc.StatusCode.already_exists: grpc.StatusCode.ALREADY_EXISTS,
    cygrpc.StatusCode.permission_denied: grpc.StatusCode.PERMISSION_DENIED,
    cygrpc.StatusCode.unauthenticated: grpc.StatusCode.UNAUTHENTICATED,
    cygrpc.StatusCode.resource_exhausted: grpc.StatusCode.RESOURCE_EXHAUSTED,
    cygrpc.StatusCode.failed_precondition: grpc.StatusCode.FAILED_PRECONDITION,
    cygrpc.StatusCode.aborted: grpc.StatusCode.ABORTED,
    cygrpc.StatusCode.out_of_range: grpc.StatusCode.OUT_OF_RANGE,
    cygrpc.StatusCode.unimplemented: grpc.StatusCode.UNIMPLEMENTED,
    cygrpc.StatusCode.internal: grpc.StatusCode.INTERNAL,
    cygrpc.StatusCode.unavailable: grpc.StatusCode.UNAVAILABLE,
    cygrpc.StatusCode.data_loss: grpc.StatusCode.DATA_LOSS,
}
STATUS_CODE_TO_CYGRPC_STATUS_CODE = {
    grpc_code: cygrpc_code
    for cygrpc_code, grpc_code in six.iteritems(
        CYGRPC_STATUS_CODE_TO_STATUS_CODE)
}


def encode(s):
    if isinstance(s, bytes):
        return s
    else:
        return s.encode('ascii')


def decode(b):
    if isinstance(b, str):
        return b
    else:
        try:
            return b.decode('utf8')
        except UnicodeDecodeError:
            logging.exception('Invalid encoding on %s', b)
            return b.decode('latin1')


def _transform(message, transformer, exception_message):
    if transformer is None:
        return message
    else:
        try:
            return transformer(message)
        except Exception:  # pylint: disable=broad-except
            logging.exception(exception_message)
            return None


def serialize(message, serializer):
    return _transform(message, serializer, 'Exception serializing message!')


def deserialize(serialized_message, deserializer):
    return _transform(serialized_message, deserializer,
                      'Exception deserializing message!')


def fully_qualified_method(group, method):
    return '/{}/{}'.format(group, method)


class CleanupThread(threading.Thread):
    """A threading.Thread subclass supporting custom behavior on join().

    On Python Interpreter exit, Python will attempt to join outstanding threads
    prior to garbage collection.  We may need to do additional cleanup, and
    we accomplish this by overriding the join() method.
    """

    def __init__(self, behavior, *args, **kwargs):
        """Constructor.

        Args:
            behavior (function): Function called on join() with a single
                argument, timeout, indicating the maximum duration of
                `behavior`, or None indicating `behavior` has no deadline.
                `behavior` must be idempotent.
            args: Positional arguments passed to threading.Thread constructor.
            kwargs: Keyword arguments passed to threading.Thread constructor.
        """
        super(CleanupThread, self).__init__(*args, **kwargs)
        self._behavior = behavior

    def join(self, timeout=None):
        start_time = time.time()
        self._behavior(timeout)
        end_time = time.time()
        if timeout is not None:
            timeout -= end_time - start_time
            timeout = max(timeout, 0)
        super(CleanupThread, self).join(timeout)
