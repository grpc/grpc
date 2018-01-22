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

import logging
import threading

import grpc
from grpc_testing import _common


class Rpc(object):

    def __init__(self, handler, invocation_metadata):
        self._condition = threading.Condition()
        self._handler = handler
        self._invocation_metadata = invocation_metadata
        self._initial_metadata_sent = False
        self._pending_trailing_metadata = None
        self._pending_code = None
        self._pending_details = None
        self._callbacks = []
        self._active = True
        self._rpc_errors = []

    def _ensure_initial_metadata_sent(self):
        if not self._initial_metadata_sent:
            self._handler.send_initial_metadata(_common.FUSSED_EMPTY_METADATA)
            self._initial_metadata_sent = True

    def _call_back(self):
        callbacks = tuple(self._callbacks)
        self._callbacks = None

        def call_back():
            for callback in callbacks:
                try:
                    callback()
                except Exception:  # pylint: disable=broad-except
                    logging.exception('Exception calling server-side callback!')

        callback_calling_thread = threading.Thread(target=call_back)
        callback_calling_thread.start()

    def _terminate(self, trailing_metadata, code, details):
        if self._active:
            self._active = False
            self._handler.send_termination(trailing_metadata, code, details)
            self._call_back()
            self._condition.notify_all()

    def _complete(self):
        if self._pending_trailing_metadata is None:
            trailing_metadata = _common.FUSSED_EMPTY_METADATA
        else:
            trailing_metadata = self._pending_trailing_metadata
        if self._pending_code is None:
            code = grpc.StatusCode.OK
        else:
            code = self._pending_code
        details = '' if self._pending_details is None else self._pending_details
        self._terminate(trailing_metadata, code, details)

    def _abort(self, code, details):
        self._terminate(_common.FUSSED_EMPTY_METADATA, code, details)

    def add_rpc_error(self, rpc_error):
        with self._condition:
            self._rpc_errors.append(rpc_error)

    def application_cancel(self):
        with self._condition:
            self._abort(grpc.StatusCode.CANCELLED,
                        'Cancelled by server-side application!')

    def application_exception_abort(self, exception):
        with self._condition:
            if exception not in self._rpc_errors:
                logging.exception('Exception calling application!')
                self._abort(
                    grpc.StatusCode.UNKNOWN,
                    'Exception calling application: {}'.format(exception))

    def extrinsic_abort(self):
        with self._condition:
            if self._active:
                self._active = False
                self._call_back()
                self._condition.notify_all()

    def unary_response_complete(self, response):
        with self._condition:
            self._ensure_initial_metadata_sent()
            self._handler.add_response(response)
            self._complete()

    def stream_response(self, response):
        with self._condition:
            self._ensure_initial_metadata_sent()
            self._handler.add_response(response)

    def stream_response_complete(self):
        with self._condition:
            self._ensure_initial_metadata_sent()
            self._complete()

    def send_initial_metadata(self, initial_metadata):
        with self._condition:
            if self._initial_metadata_sent:
                return False
            else:
                self._handler.send_initial_metadata(initial_metadata)
                self._initial_metadata_sent = True
                return True

    def is_active(self):
        with self._condition:
            return self._active

    def add_callback(self, callback):
        with self._condition:
            if self._callbacks is None:
                return False
            else:
                self._callbacks.append(callback)
                return True

    def invocation_metadata(self):
        with self._condition:
            return self._invocation_metadata

    def set_trailing_metadata(self, trailing_metadata):
        with self._condition:
            self._pending_trailing_metadata = trailing_metadata

    def set_code(self, code):
        with self._condition:
            self._pending_code = code

    def set_details(self, details):
        with self._condition:
            self._pending_details = details
