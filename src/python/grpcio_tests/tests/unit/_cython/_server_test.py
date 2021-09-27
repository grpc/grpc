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
"""Test servers at the level of the Cython API."""

import threading
import time
import unittest

from grpc._cython import cygrpc


class Test(unittest.TestCase):

    def test_lonely_server(self):
        server_call_completion_queue = cygrpc.CompletionQueue()
        server_shutdown_completion_queue = cygrpc.CompletionQueue()
        server = cygrpc.Server(None, False)
        server.register_completion_queue(server_call_completion_queue)
        server.register_completion_queue(server_shutdown_completion_queue)
        port = server.add_http2_port(b'[::]:0')
        server.start()

        server_request_call_tag = 'server_request_call_tag'
        server_request_call_start_batch_result = server.request_call(
            server_call_completion_queue, server_call_completion_queue,
            server_request_call_tag)

        time.sleep(4)

        server_shutdown_tag = 'server_shutdown_tag'
        server_shutdown_result = server.shutdown(
            server_shutdown_completion_queue, server_shutdown_tag)
        server_request_call_event = server_call_completion_queue.poll()
        server_shutdown_event = server_shutdown_completion_queue.poll()


if __name__ == '__main__':
    unittest.main(verbosity=2)
