# Copyright 2016 gRPC authors.
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
"""Tests of channel arguments on client/server side."""

from concurrent import futures
import unittest
import logging

import grpc


class TestPointerWrapper(object):

    def __int__(self):
        return 123456


TEST_CHANNEL_ARGS = (
    ('arg1', b'bytes_val'),
    ('arg2', 'str_val'),
    ('arg3', 1),
    (b'arg4', 'str_val'),
    ('arg6', TestPointerWrapper()),
)

INVALID_TEST_CHANNEL_ARGS = [
    {
        'foo': 'bar'
    },
    (('key',),),
    'str',
]


class ChannelArgsTest(unittest.TestCase):

    def test_client(self):
        grpc.insecure_channel('localhost:8080', options=TEST_CHANNEL_ARGS)

    def test_server(self):
        grpc.server(futures.ThreadPoolExecutor(max_workers=1),
                    options=TEST_CHANNEL_ARGS)

    def test_invalid_client_args(self):
        for invalid_arg in INVALID_TEST_CHANNEL_ARGS:
            self.assertRaises(ValueError,
                              grpc.insecure_channel,
                              'localhost:8080',
                              options=invalid_arg)


if __name__ == '__main__':
    logging.basicConfig()
    unittest.main(verbosity=2)
