# Copyright 2015, gRPC authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""Affords a connectivity-state-listenable channel."""

import threading
import time

from grpc._adapter import _low
from grpc._adapter import _types
from grpc.beta import interfaces
from grpc.framework.foundation import callable_util

_CHANNEL_SUBSCRIPTION_CALLBACK_ERROR_LOG_MESSAGE = (
    'Exception calling channel subscription callback!')

_LOW_CONNECTIVITY_STATE_TO_CHANNEL_CONNECTIVITY = {
    state: connectivity
    for state, connectivity in zip(_types.ConnectivityState,
                                   interfaces.ChannelConnectivity)
}


class ConnectivityChannel(object):

    def __init__(self, low_channel):
        self._lock = threading.Lock()
        self._low_channel = low_channel

        self._polling = False
        self._connectivity = None
        self._try_to_connect = False
        self._callbacks_and_connectivities = []
        self._delivering = False

    def _deliveries(self, connectivity):
        callbacks_needing_update = []
        for callback_and_connectivity in self._callbacks_and_connectivities:
            callback, callback_connectivity = callback_and_connectivity
            if callback_connectivity is not connectivity:
                callbacks_needing_update.append(callback)
                callback_and_connectivity[1] = connectivity
        return callbacks_needing_update

    def _deliver(self, initial_connectivity, initial_callbacks):
        connectivity = initial_connectivity
        callbacks = initial_callbacks
        while True:
            for callback in callbacks:
                callable_util.call_logging_exceptions(
                    callback, _CHANNEL_SUBSCRIPTION_CALLBACK_ERROR_LOG_MESSAGE,
                    connectivity)
            with self._lock:
                callbacks = self._deliveries(self._connectivity)
                if callbacks:
                    connectivity = self._connectivity
                else:
                    self._delivering = False
                    return

    def _spawn_delivery(self, connectivity, callbacks):
        delivering_thread = threading.Thread(
            target=self._deliver, args=(connectivity, callbacks,))
        delivering_thread.start()
        self._delivering = True

    # TODO(issue 3064): Don't poll.
    def _poll_connectivity(self, low_channel, initial_try_to_connect):
        try_to_connect = initial_try_to_connect
        low_connectivity = low_channel.check_connectivity_state(try_to_connect)
        with self._lock:
            self._connectivity = _LOW_CONNECTIVITY_STATE_TO_CHANNEL_CONNECTIVITY[
                low_connectivity]
            callbacks = tuple(
                callback
                for callback, unused_but_known_to_be_none_connectivity in
                self._callbacks_and_connectivities)
            for callback_and_connectivity in self._callbacks_and_connectivities:
                callback_and_connectivity[1] = self._connectivity
            if callbacks:
                self._spawn_delivery(self._connectivity, callbacks)
        completion_queue = _low.CompletionQueue()
        while True:
            low_channel.watch_connectivity_state(low_connectivity,
                                                 time.time() + 0.2,
                                                 completion_queue, None)
            event = completion_queue.next()
            with self._lock:
                if not self._callbacks_and_connectivities and not self._try_to_connect:
                    self._polling = False
                    self._connectivity = None
                    completion_queue.shutdown()
                    break
                try_to_connect = self._try_to_connect
                self._try_to_connect = False
            if event.success or try_to_connect:
                low_connectivity = low_channel.check_connectivity_state(
                    try_to_connect)
                with self._lock:
                    self._connectivity = _LOW_CONNECTIVITY_STATE_TO_CHANNEL_CONNECTIVITY[
                        low_connectivity]
                    if not self._delivering:
                        callbacks = self._deliveries(self._connectivity)
                        if callbacks:
                            self._spawn_delivery(self._connectivity, callbacks)

    def subscribe(self, callback, try_to_connect):
        with self._lock:
            if not self._callbacks_and_connectivities and not self._polling:
                polling_thread = threading.Thread(
                    target=self._poll_connectivity,
                    args=(self._low_channel, bool(try_to_connect)))
                polling_thread.start()
                self._polling = True
                self._callbacks_and_connectivities.append([callback, None])
            elif not self._delivering and self._connectivity is not None:
                self._spawn_delivery(self._connectivity, (callback,))
                self._try_to_connect |= bool(try_to_connect)
                self._callbacks_and_connectivities.append(
                    [callback, self._connectivity])
            else:
                self._try_to_connect |= bool(try_to_connect)
                self._callbacks_and_connectivities.append([callback, None])

    def unsubscribe(self, callback):
        with self._lock:
            for index, (subscribed_callback, unused_connectivity
                       ) in enumerate(self._callbacks_and_connectivities):
                if callback == subscribed_callback:
                    self._callbacks_and_connectivities.pop(index)
                    break

    def low_channel(self):
        return self._low_channel
