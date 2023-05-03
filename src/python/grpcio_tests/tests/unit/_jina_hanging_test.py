# Copyright 2023 gRPC authors.
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
import unittest
import multiprocessing
from concurrent import futures

import grpc

_REQUEST = b'\x00\x00\x00'
_RESPONSE = b'\x00\x00\x01'

_UNARY_UNARY = '/test/UnaryUnary'


def _handle_unary_unary(unused_request, unused_servicer_context):
  return _RESPONSE

def run_server(port, event):
  handler = grpc.method_handlers_generic_handler('test', {
    'UnaryUnary':
      grpc.unary_unary_rpc_method_handler(_handle_unary_unary)
  })
  server = grpc.server(futures.ThreadPoolExecutor(max_workers=10), (handler,))
  bind_addr = f'0.0.0.0:{port}'
  server.add_insecure_port(bind_addr)
  server.start()
  event.set()
  server.wait_for_termination()

def run_client(port):
  addr = f'0.0.0.0:{port}'
  channel = grpc.insecure_channel(addr)
  multi_callable = channel.unary_unary(_UNARY_UNARY)
  multi_callable(_REQUEST)
  channel.close()


class ReconnectTest(unittest.TestCase):

  def test_reconnect(self):
    for _ in range(3):
      import random

      port = random.randint(12345, 22345)
      event = multiprocessing.Event()
      p = multiprocessing.Process(target=run_server, args=(port, event))
      p.start()
      event.wait()
      run_client(port)
      p.terminate()
      p.join()


if __name__ == '__main__':
  logging.basicConfig()
  unittest.main(verbosity=2)
