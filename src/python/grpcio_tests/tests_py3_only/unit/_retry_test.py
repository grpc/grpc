# Copyright 2020 The gRPC Authors
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
import unittest
import logging
import json
from concurrent import futures

import grpc

_TEST_METHOD = '/test/Test'
_REQUEST = b'\x23\x33'


class _GenericHandler(grpc.GenericRpcHandler):

    @staticmethod
    def _abort_handler(request, context):
        logging.info('Server received: %s', request)
        context.abort(grpc.StatusCode.UNAVAILABLE, 'Aborted by test server')

    def service(self, handler_call_details):
        if handler_call_details.method == _TEST_METHOD:
            return grpc.unary_unary_rpc_method_handler(self._abort_handler)


def _start_a_test_server():
    server = grpc.server(futures.ThreadPoolExecutor(max_workers=1),
                         options=(('grpc.so_reuseport', 0),))
    server.add_generic_rpc_handlers((_GenericHandler(),))
    port = server.add_insecure_port('localhost:0')
    server.start()
    return 'localhost:%d' % port, server


class TestRetry(unittest.TestCase):

    def test_leak_with_single_shot_rpcs(self):
        address, server = _start_a_test_server()

        json_config = json.dumps({
            "methodConfig": [{
                "name": [{}],
                "retryPolicy": {
                    "maxAttempts": 5,
                    "initialBackoff": "0.1s",
                    "maxBackoff": "3s",
                    "backoffMultiplier": 5,
                    "retryableStatusCodes": ["UNAVAILABLE"],
                },
            }]
        })
        options = []
        options.append(("grpc.enable_retries", 1))
        options.append(('grpc.keepalive_timeout_ms', 10000))
        options.append(('grpc.lb_policy_name', 'pick_first'))
        options.append(("grpc.service_config", json_config))

        with grpc.insecure_channel(address, options=options) as channel:
            multicallable = channel.unary_unary(_TEST_METHOD)
            response = multicallable(_REQUEST)
            assert _REQUEST == response
        logging.info("Greeter client received: %s", response)


if __name__ == "__main__":
    logging.basicConfig(level=logging.DEBUG)
    unittest.main(verbosity=2)
