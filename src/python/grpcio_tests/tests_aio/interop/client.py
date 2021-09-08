# Copyright 2019 The gRPC Authors
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

import argparse
import asyncio
import logging
import os

import grpc
from grpc.experimental import aio

from tests.interop import client as interop_client_lib
from tests_aio.interop import methods

_LOGGER = logging.getLogger(__name__)
_LOGGER.setLevel(logging.DEBUG)


def _create_channel(args):
    target = f'{args.server_host}:{args.server_port}'

    if args.use_tls or args.use_alts or args.custom_credentials_type is not None:
        channel_credentials, options = interop_client_lib.get_secure_channel_parameters(
            args)
        return aio.secure_channel(target, channel_credentials, options)
    else:
        return aio.insecure_channel(target)


def _test_case_from_arg(test_case_arg):
    for test_case in methods.TestCase:
        if test_case_arg == test_case.value:
            return test_case
    else:
        raise ValueError('No test case "%s"!' % test_case_arg)


async def test_interoperability():

    args = interop_client_lib.parse_interop_client_args()
    channel = _create_channel(args)
    stub = interop_client_lib.create_stub(channel, args)
    test_case = _test_case_from_arg(args.test_case)
    await methods.test_interoperability(test_case, stub, args)


if __name__ == '__main__':
    logging.basicConfig(level=logging.DEBUG)
    asyncio.get_event_loop().set_debug(True)
    asyncio.get_event_loop().run_until_complete(test_interoperability())
