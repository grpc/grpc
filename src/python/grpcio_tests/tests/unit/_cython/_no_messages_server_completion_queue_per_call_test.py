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

        client_call = self.channel.create_call(
            None, _common.EMPTY_FLAGS, self.client_completion_queue,
            b'/twinkies', None, _common.INFINITE_FUTURE)
        client_receive_initial_metadata_tag = 'client_receive_initial_metadata_tag'
        client_complete_rpc_tag = 'client_complete_rpc_tag'
        with self.client_condition:
            client_receive_initial_metadata_start_batch_result = (
                client_call.start_client_batch(
                    cygrpc.Operations([
                        cygrpc.operation_receive_initial_metadata(
                            _common.EMPTY_FLAGS),
                    ]), client_receive_initial_metadata_tag))
            client_complete_rpc_start_batch_result = client_call.start_client_batch(
                cygrpc.Operations([
                    cygrpc.operation_send_initial_metadata(
                        _common.INVOCATION_METADATA, _common.EMPTY_FLAGS),
                    cygrpc.operation_send_close_from_client(
                        _common.EMPTY_FLAGS),
                    cygrpc.operation_receive_status_on_client(
                        _common.EMPTY_FLAGS),
                ]), client_complete_rpc_tag)
            self.client_driver.add_due({
                client_receive_initial_metadata_tag,
                client_complete_rpc_tag,
            })

        server_request_call_event = self.server_driver.event_with_tag(
            server_request_call_tag)

        with server_call_condition:
            server_send_initial_metadata_start_batch_result = (
                server_request_call_event.operation_call.start_server_batch([
                    cygrpc.operation_send_initial_metadata(
                        _common.INITIAL_METADATA, _common.EMPTY_FLAGS),
                ], server_send_initial_metadata_tag))
            server_call_driver.add_due({
                server_send_initial_metadata_tag,
            })
        server_send_initial_metadata_event = server_call_driver.event_with_tag(
            server_send_initial_metadata_tag)

        with server_call_condition:
            server_complete_rpc_start_batch_result = (
                server_request_call_event.operation_call.start_server_batch([
                    cygrpc.operation_receive_close_on_server(
                        _common.EMPTY_FLAGS),
                    cygrpc.operation_send_status_from_server(
                        _common.TRAILING_METADATA, cygrpc.StatusCode.ok,
                        b'test details', _common.EMPTY_FLAGS),
                ], server_complete_rpc_tag))
            server_call_driver.add_due({
                server_complete_rpc_tag,
            })
        server_complete_rpc_event = server_call_driver.event_with_tag(
            server_complete_rpc_tag)

        client_receive_initial_metadata_event = self.client_driver.event_with_tag(
            client_receive_initial_metadata_tag)
        client_complete_rpc_event = self.client_driver.event_with_tag(
            client_complete_rpc_tag)

        return (_common.OperationResult(server_request_call_start_batch_result,
                                        server_request_call_event.type,
                                        server_request_call_event.success),
                _common.OperationResult(
                    client_receive_initial_metadata_start_batch_result,
                    client_receive_initial_metadata_event.type,
                    client_receive_initial_metadata_event.success),
                _common.OperationResult(client_complete_rpc_start_batch_result,
                                        client_complete_rpc_event.type,
                                        client_complete_rpc_event.success),
                _common.OperationResult(
                    server_send_initial_metadata_start_batch_result,
                    server_send_initial_metadata_event.type,
                    server_send_initial_metadata_event.success),
                _common.OperationResult(server_complete_rpc_start_batch_result,
                                        server_complete_rpc_event.type,
                                        server_complete_rpc_event.success),)

    def test_rpcs(self):
        expecteds = [(_common.SUCCESSFUL_OPERATION_RESULT,) *
                     5] * _common.RPC_COUNT
        actuallys = _common.execute_many_times(self._do_rpcs)
        self.assertSequenceEqual(expecteds, actuallys)


if __name__ == '__main__':
    unittest.main(verbosity=2)
