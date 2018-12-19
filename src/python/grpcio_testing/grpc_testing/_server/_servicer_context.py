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

import grpc
from grpc_testing import _common


class ServicerContext(grpc.ServicerContext):

    def __init__(self, rpc, time, deadline):
        self._rpc = rpc
        self._time = time
        self._deadline = deadline

    def is_active(self):
        return self._rpc.is_active()

    def time_remaining(self):
        if self._rpc.is_active():
            if self._deadline is None:
                return None
            else:
                return max(0.0, self._deadline - self._time.time())
        else:
            return 0.0

    def cancel(self):
        self._rpc.application_cancel()

    def add_callback(self, callback):
        return self._rpc.add_callback(callback)

    def invocation_metadata(self):
        return self._rpc.invocation_metadata()

    def peer(self):
        raise NotImplementedError()

    def peer_identities(self):
        raise NotImplementedError()

    def peer_identity_key(self):
        raise NotImplementedError()

    def auth_context(self):
        raise NotImplementedError()

    def send_initial_metadata(self, initial_metadata):
        initial_metadata_sent = self._rpc.send_initial_metadata(
            _common.fuss_with_metadata(initial_metadata))
        if not initial_metadata_sent:
            raise ValueError(
                'ServicerContext.send_initial_metadata called too late!')

    def set_trailing_metadata(self, trailing_metadata):
        self._rpc.set_trailing_metadata(
            _common.fuss_with_metadata(trailing_metadata))

    def abort(self, code, details):
        raise NotImplementedError()

    def abort_with_status(self, status):
        raise NotImplementedError()

    def set_code(self, code):
        self._rpc.set_code(code)

    def set_details(self, details):
        self._rpc.set_details(details)
