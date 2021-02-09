# Copyright 2019 the gRPC authors.
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
"""Client for testing responsiveness to signals."""

from __future__ import print_function

import argparse
import functools
import logging
import signal
import sys

import grpc

SIGTERM_MESSAGE = "Handling sigterm!"

UNARY_UNARY = "/test/Unary"
UNARY_STREAM = "/test/ServerStreaming"

_MESSAGE = b'\x00\x00\x00'

_ASSERTION_MESSAGE = "Control flow should never reach here."

# NOTE(gnossen): We use a global variable here so that the signal handler can be
# installed before the RPC begins. If we do not do this, then we may receive the
# SIGINT before the signal handler is installed. I'm not happy with per-process
# global state, but the per-process global state that is signal handlers
# somewhat forces my hand.
per_process_rpc_future = None


def handle_sigint(unused_signum, unused_frame):
    print(SIGTERM_MESSAGE)
    if per_process_rpc_future is not None:
        per_process_rpc_future.cancel()
    sys.stderr.flush()
    # This sys.exit(0) avoids an exception caused by the cancelled RPC.
    sys.exit(0)


def main_unary(server_target):
    """Initiate a unary RPC to be interrupted by a SIGINT."""
    global per_process_rpc_future  # pylint: disable=global-statement
    with grpc.insecure_channel(server_target) as channel:
        multicallable = channel.unary_unary(UNARY_UNARY)
        signal.signal(signal.SIGINT, handle_sigint)
        per_process_rpc_future = multicallable.future(_MESSAGE,
                                                      wait_for_ready=True)
        result = per_process_rpc_future.result()
        assert False, _ASSERTION_MESSAGE


def main_streaming(server_target):
    """Initiate a streaming RPC to be interrupted by a SIGINT."""
    global per_process_rpc_future  # pylint: disable=global-statement
    with grpc.insecure_channel(server_target) as channel:
        signal.signal(signal.SIGINT, handle_sigint)
        per_process_rpc_future = channel.unary_stream(UNARY_STREAM)(
            _MESSAGE, wait_for_ready=True)
        for result in per_process_rpc_future:
            pass
        assert False, _ASSERTION_MESSAGE


def main_unary_with_exception(server_target):
    """Initiate a unary RPC with a signal handler that will raise."""
    channel = grpc.insecure_channel(server_target)
    try:
        channel.unary_unary(UNARY_UNARY)(_MESSAGE, wait_for_ready=True)
    except KeyboardInterrupt:
        sys.stderr.write("Running signal handler.\n")
        sys.stderr.flush()

    # This call should not freeze.
    channel.close()


def main_streaming_with_exception(server_target):
    """Initiate a streaming RPC with a signal handler that will raise."""
    channel = grpc.insecure_channel(server_target)
    try:
        for _ in channel.unary_stream(UNARY_STREAM)(_MESSAGE,
                                                    wait_for_ready=True):
            pass
    except KeyboardInterrupt:
        sys.stderr.write("Running signal handler.\n")
        sys.stderr.flush()

    # This call should not freeze.
    channel.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Signal test client.')
    parser.add_argument('server', help='Server target')
    parser.add_argument('arity', help='Arity', choices=('unary', 'streaming'))
    parser.add_argument('--exception',
                        help='Whether the signal throws an exception',
                        action='store_true')
    args = parser.parse_args()
    if args.arity == 'unary' and not args.exception:
        main_unary(args.server)
    elif args.arity == 'streaming' and not args.exception:
        main_streaming(args.server)
    elif args.arity == 'unary' and args.exception:
        main_unary_with_exception(args.server)
    else:
        main_streaming_with_exception(args.server)
