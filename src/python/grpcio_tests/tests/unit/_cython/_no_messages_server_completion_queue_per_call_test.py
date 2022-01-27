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
"""Test a corner-case at the level of the Cython API."""

import threading
import unittest

from grpc._cython import cygrpc

from tests.unit._cython import _common
from tests.unit._cython import test_utilities


class Test(_common.RpcTest, unittest.TestCase):

    def _do_rpcs(self):
        server_call_condition = threading.Condition()
        server_call_completion_queue = cygrpc.CompletionQueue()
        server_call_driver = _common.QueueDriver(server_call_condition,
                                                 server_call_completion_queue)

        server_request_call_tag = 'server_request_call_tag'
        server_send_initial_metadata_tag = 'server_send_initial_metadata_tag'
        server_complete_rpc_tag = 'server_complete_rpc_tag'

        with self.server_condition:
            server_request_call_start_batch_result = self.server.request_call(
                server_call_completion_queue, self.server_completion_queue,
                server_request_call_tag)
            self.server_driver.add_due({
                server_request_call_tag,
            })

        client_receive_initial_metadata_tag = 'client_receive_initial_metadata_tag'
        client_complete_rpc_tag = 'client_complete_rpc_tag'
        client_call = self.channel.integrated_call(
            _common.EMPTY_FLAGS, b'/twinkies', None, None,
            _common.INVOCATION_METADATA, None, [(
                [
                    cygrpc.ReceiveInitialMetadataOperation(_common.EMPTY_FLAGS),
                ],
                client_receive_initial_metadata_tag,
            )])
        client_call.operate([
            cygrpc.SendInitialMetadataOperation(_common.INVOCATION_METADATA,
                                                _common.EMPTY_FLAGS),
            cygrpc.SendCloseFromClientOperation(_common.EMPTY_FLAGS),
            cygrpc.ReceiveStatusOnClientOperation(_common.EMPTY_FLAGS),
        ], client_complete_rpc_tag)

        client_events_future = test_utilities.SimpleFuture(lambda: [
            self.channel.next_call_event(),
            self.channel.next_call_event(),
        ])

        server_request_call_event = self.server_driver.event_with_tag(
            server_request_call_tag)

        with server_call_condition:
            server_send_initial_metadata_start_batch_result = (
                server_request_call_event.call.start_server_batch([
                    cygrpc.SendInitialMetadataOperation(
                        _common.INITIAL_METADATA, _common.EMPTY_FLAGS),
                ], server_send_initial_metadata_tag))
            server_call_driver.add_due({
                server_send_initial_metadata_tag,
            })
        server_send_initial_metadata_event = server_call_driver.event_with_tag(
            server_send_initial_metadata_tag)

        with server_call_condition:
            server_complete_rpc_start_batch_result = (
                server_request_call_event.call.start_server_batch([
                    cygrpc.ReceiveCloseOnServerOperation(_common.EMPTY_FLAGS),
                    cygrpc.SendStatusFromServerOperation(
                        _common.TRAILING_METADATA, cygrpc.StatusCode.ok,
                        b'test details', _common.EMPTY_FLAGS),
                ], server_complete_rpc_tag))
            server_call_driver.add_due({
                server_complete_rpc_tag,
            })
        server_complete_rpc_event = server_call_driver.event_with_tag(
            server_complete_rpc_tag)

        client_events = client_events_future.result()
        if client_events[0].tag is client_receive_initial_metadata_tag:
            client_receive_initial_metadata_event = client_events[0]
            client_complete_rpc_event = client_events[1]
        else:
            client_complete_rpc_event = client_events[0]
            client_receive_initial_metadata_event = client_events[1]

        return (
            _common.OperationResult(server_request_call_start_batch_result,
                                    server_request_call_event.completion_type,
                                    server_request_call_event.success),
            _common.OperationResult(
                cygrpc.CallError.ok,
                client_receive_initial_metadata_event.completion_type,
                client_receive_initial_metadata_event.success),
            _common.OperationResult(cygrpc.CallError.ok,
                                    client_complete_rpc_event.completion_type,
                                    client_complete_rpc_event.success),
            _common.OperationResult(
                server_send_initial_metadata_start_batch_result,
                server_send_initial_metadata_event.completion_type,
                server_send_initial_metadata_event.success),
            _common.OperationResult(server_complete_rpc_start_batch_result,
                                    server_complete_rpc_event.completion_type,
                                    server_complete_rpc_event.success),
        )

    def test_rpcs(self):
        expecteds = [(_common.SUCCESSFUL_OPERATION_RESULT,) * 5
                    ] * _common.RPC_COUNT
        actuallys = _common.execute_many_times(self._do_rpcs)
        self.assertSequenceEqual(expecteds, actuallys)


if __name__ == '__main__':
    unittest.main(verbosity=2)
