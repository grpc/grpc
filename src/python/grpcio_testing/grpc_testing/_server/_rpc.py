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
from typing import Any, Callable, Iterable, List, Optional

import grpc
from grpc._typing import MetadataType
from grpc_testing import _common
from grpc_testing import _handler

logging.basicConfig()
_LOGGER = logging.getLogger(__name__)


class Rpc(object):
    _condition: threading.Condition
    _handler: _handler.Handler
    _invocation_metadata: MetadataType
    _initial_metadata_sent: bool
    _pending_trailing_metadata: Optional[MetadataType]
    _pending_code: Optional[grpc.StatusCode]
    _pending_details: Optional[str]
    _callbacks: Optional[List[Callable[[], Any]]]
    _active: bool
    _rpc_errors: List[grpc.RpcError]

    def __init__(self, handler: _handler.Handler,
                 invocation_metadata: MetadataType):
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

    def _ensure_initial_metadata_sent(self) -> None:
        if not self._initial_metadata_sent:
            self._handler.send_initial_metadata(_common.FUSSED_EMPTY_METADATA)
            self._initial_metadata_sent = True

    def _call_back(self) -> None:
        callbacks = tuple(self._callbacks)  # type: ignore[arg-type]
        self._callbacks = None

        def call_back() -> None:
            for callback in callbacks:
                try:
                    callback()
                except Exception:  # pylint: disable=broad-except
                    _LOGGER.exception("Exception calling server-side callback!")

        callback_calling_thread = threading.Thread(target=call_back)
        callback_calling_thread.start()

    def _terminate(self, trailing_metadata: MetadataType, code: grpc.StatusCode,
                   details: str) -> None:
        if self._active:
            self._active = False
            self._handler.send_termination(trailing_metadata, code, details)
            self._call_back()
            self._condition.notify_all()

    def _complete(self) -> None:
        if self._pending_trailing_metadata is None:
            trailing_metadata = _common.FUSSED_EMPTY_METADATA
        else:
            trailing_metadata = self._pending_trailing_metadata
        if self._pending_code is None:
            code = grpc.StatusCode.OK
        else:
            code = self._pending_code
        details = "" if self._pending_details is None else self._pending_details
        self._terminate(trailing_metadata, code, details)

    def _abort(self, code: grpc.StatusCode, details: str) -> None:
        self._terminate(_common.FUSSED_EMPTY_METADATA, code, details)

    def add_rpc_error(self, rpc_error: grpc.RpcError) -> None:
        with self._condition:
            self._rpc_errors.append(rpc_error)  # ty1pe: ignore

    def application_cancel(self) -> None:
        with self._condition:
            self._abort(
                grpc.StatusCode.CANCELLED,
                "Cancelled by server-side application!",
            )

    def application_exception_abort(self, exception: Exception) -> None:
        with self._condition:
            if exception not in self._rpc_errors:
                _LOGGER.exception("Exception calling application!")
                self._abort(
                    grpc.StatusCode.UNKNOWN,
                    "Exception calling application: {}".format(exception),
                )

    def extrinsic_abort(self) -> None:
        with self._condition:
            if self._active:
                self._active = False
                self._call_back()
                self._condition.notify_all()

    def unary_response_complete(self, response: Any) -> None:
        with self._condition:
            self._ensure_initial_metadata_sent()
            self._handler.add_response(response)
            self._complete()

    def stream_response(self, response: Any) -> None:
        with self._condition:
            self._ensure_initial_metadata_sent()
            self._handler.add_response(response)

    def stream_response_complete(self) -> None:
        with self._condition:
            self._ensure_initial_metadata_sent()
            self._complete()

    def send_initial_metadata(self, initial_metadata: MetadataType) -> bool:
        with self._condition:
            if self._initial_metadata_sent:
                return False
            else:
                self._handler.send_initial_metadata(initial_metadata)
                self._initial_metadata_sent = True
                return True

    def is_active(self) -> bool:
        with self._condition:
            return self._active

    def add_callback(self, callback: Callable[[], Any]) -> bool:
        with self._condition:
            if self._callbacks is None:
                return False
            else:
                if not self._callbacks:
                    self._callbacks = []
                self._callbacks.append(callback)
                return True

    def invocation_metadata(self) -> Optional[MetadataType]:
        with self._condition:
            return self._invocation_metadata

    def set_trailing_metadata(
            self, trailing_metadata: Optional[MetadataType]) -> None:
        with self._condition:
            self._pending_trailing_metadata = trailing_metadata

    def set_code(self, code: grpc.StatusCode) -> None:
        with self._condition:
            self._pending_code = code

    def set_details(self, details: str) -> None:
        with self._condition:
            self._pending_details = details
