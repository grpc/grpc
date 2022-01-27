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

import collections
import threading

from grpc_testing import _common
from grpc_testing._channel import _rpc_state


class State(_common.ChannelHandler):

    def __init__(self):
        self._condition = threading.Condition()
        self._rpc_states = collections.defaultdict(list)

    def invoke_rpc(self, method_full_rpc_name, invocation_metadata, requests,
                   requests_closed, timeout):
        rpc_state = _rpc_state.State(invocation_metadata, requests,
                                     requests_closed)
        with self._condition:
            self._rpc_states[method_full_rpc_name].append(rpc_state)
            self._condition.notify_all()
        return rpc_state

    def take_rpc_state(self, method_descriptor):
        method_full_rpc_name = '/{}/{}'.format(
            method_descriptor.containing_service.full_name,
            method_descriptor.name)
        with self._condition:
            while True:
                method_rpc_states = self._rpc_states[method_full_rpc_name]
                if method_rpc_states:
                    return method_rpc_states.pop(0)
                else:
                    self._condition.wait()
