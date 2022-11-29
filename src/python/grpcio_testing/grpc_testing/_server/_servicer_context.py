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

from typing import Any, Callable, Mapping, NoReturn, Optional, Sequence

import grpc
from grpc._typing import MetadataType
from grpc_testing import _common
from grpc_testing._server import _rpc


class ServicerContext(grpc.ServicerContext):
    _rpc: _rpc.Rpc
    _time: float
    _deadline: float

    def __init__(self, rpc: _rpc.Rpc, time: float, deadline: float):
        self._rpc = rpc
        self._time = time
        self._deadline = deadline

    def is_active(self) -> bool:
        return self._rpc.is_active()

    def time_remaining(self) -> Optional[float]:
        if self._rpc.is_active():
            if self._deadline is None:
                return None
            else:
                return max(0.0, self._deadline - self._time.time())
        else:
            return 0.0

    def cancel(self) -> bool:
        self._rpc.application_cancel()

    def add_callback(self, callback: Callable[[], Any]) -> bool:
        return self._rpc.add_callback(callback)

    def invocation_metadata(self) -> Optional[MetadataType]:
        return self._rpc.invocation_metadata()

    def peer(self) -> str:
        raise NotImplementedError()

    def peer_identities(self) -> Optional[Sequence[bytes]]:
        raise NotImplementedError()

    def peer_identity_key(self) -> Optional[str]:
        raise NotImplementedError()

    def auth_context(self) -> Mapping[str, Sequence[bytes]]:
        raise NotImplementedError()

    def set_compression(self) -> None:
        raise NotImplementedError()

    def send_initial_metadata(self,
                              initial_metadata: Optional[MetadataType]) -> None:
        initial_metadata_sent = self._rpc.send_initial_metadata(
            _common.fuss_with_metadata(initial_metadata))
        if not initial_metadata_sent:
            raise ValueError(
                'ServicerContext.send_initial_metadata called too late!')

    def disable_next_message_compression(self) -> None:
        raise NotImplementedError()

    def set_trailing_metadata(
            self, trailing_metadata: Optional[MetadataType]) -> None:
        self._rpc.set_trailing_metadata(
            _common.fuss_with_metadata(trailing_metadata))

    def abort(self, code: grpc.StatusCode, details: str) -> NoReturn:
        with self._rpc._condition:
            self._rpc._abort(code, details)
        raise Exception()

    def abort_with_status(self, status: grpc.Status) -> NoReturn:
        raise NotImplementedError()

    def set_code(self, code: grpc.StatusCode) -> None:
        self._rpc.set_code(code)

    def set_details(self, details: str) -> None:
        self._rpc.set_details(details)
