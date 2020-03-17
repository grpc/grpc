# Copyright 2020 The gRPC authors.
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
import logging
import signal
import threading
import time
import sys

from typing import DefaultDict, Dict, List, Mapping, Set
import collections

from concurrent import futures

import grpc

from src.proto.grpc.testing import test_pb2
from src.proto.grpc.testing import test_pb2_grpc
from src.proto.grpc.testing import messages_pb2
from src.proto.grpc.testing import empty_pb2

logger = logging.getLogger()
console_handler = logging.StreamHandler()
formatter = logging.Formatter(fmt='%(asctime)s: %(levelname)-8s %(message)s')
console_handler.setFormatter(formatter)
logger.addHandler(console_handler)


class _StatsWatcher:
    _start: int
    _end: int
    _rpcs_needed: int
    _rpcs_by_peer: DefaultDict[str, int]
    _no_remote_peer: int
    _lock: threading.Lock
    _condition: threading.Condition

    def __init__(self, start: int, end: int):
        self._start = start
        self._end = end
        self._rpcs_needed = end - start
        self._rpcs_by_peer = collections.defaultdict(int)
        self._condition = threading.Condition()
        self._no_remote_peer = 0

    def on_rpc_complete(self, request_id: int, peer: str) -> None:
        """Records statistics for a single RPC."""
        if self._start <= request_id < self._end:
            with self._condition:
                if not peer:
                    self._no_remote_peer += 1
                else:
                    self._rpcs_by_peer[peer] += 1
                self._rpcs_needed -= 1
                self._condition.notify()

    def await_rpc_stats_response(self, timeout_sec: int
                                ) -> messages_pb2.LoadBalancerStatsResponse:
        """Blocks until a full response has been collected."""
        with self._condition:
            self._condition.wait_for(lambda: not self._rpcs_needed,
                                     timeout=float(timeout_sec))
            response = messages_pb2.LoadBalancerStatsResponse()
            for peer, count in self._rpcs_by_peer.items():
                response.rpcs_by_peer[peer] = count
            response.num_failures = self._no_remote_peer + self._rpcs_needed
        return response


_global_lock = threading.Lock()
_stop_event = threading.Event()
_global_rpc_id: int = 0
_watchers: Set[_StatsWatcher] = set()
_global_server = None


def _handle_sigint(sig, frame):
    _stop_event.set()
    _global_server.stop(None)


class _LoadBalancerStatsServicer(test_pb2_grpc.LoadBalancerStatsServiceServicer
                                ):

    def __init__(self):
        super(_LoadBalancerStatsServicer).__init__()

    def GetClientStats(self, request: messages_pb2.LoadBalancerStatsRequest,
                       context: grpc.ServicerContext
                      ) -> messages_pb2.LoadBalancerStatsResponse:
        logger.info("Received stats request.")
        start = None
        end = None
        watcher = None
        with _global_lock:
            start = _global_rpc_id + 1
            end = start + request.num_rpcs
            watcher = _StatsWatcher(start, end)
            _watchers.add(watcher)
        response = watcher.await_rpc_stats_response(request.timeout_sec)
        with _global_lock:
            _watchers.remove(watcher)
        logger.info("Returning stats response: {}".format(response))
        return response


def _start_rpc(request_id: int, stub: test_pb2_grpc.TestServiceStub,
               timeout: float, futures: Mapping[int, grpc.Future]) -> None:
    logger.info(f"Sending request to backend: {request_id}")
    future = stub.UnaryCall.future(messages_pb2.SimpleRequest(),
                                   timeout=timeout)
    futures[request_id] = future


def _on_rpc_done(rpc_id: int, future: grpc.Future,
                 print_response: bool) -> None:
    exception = future.exception()
    hostname = ""
    if exception is not None:
        if exception.code() == grpc.StatusCode.DEADLINE_EXCEEDED:
            logger.error(f"RPC {rpc_id} timed out")
        else:
            logger.error(exception)
    else:
        response = future.result()
        logger.info(f"Got result {rpc_id}")
        hostname = response.hostname
        if print_response:
            if future.code() == grpc.StatusCode.OK:
                logger.info("Successful response.")
            else:
                logger.info(f"RPC failed: {call}")
    with _global_lock:
        for watcher in _watchers:
            watcher.on_rpc_complete(rpc_id, hostname)


def _remove_completed_rpcs(futures: Mapping[int, grpc.Future],
                           print_response: bool) -> None:
    logger.debug("Removing completed RPCs")
    done = []
    for future_id, future in futures.items():
        if future.done():
            _on_rpc_done(future_id, future, args.print_response)
            done.append(future_id)
    for rpc_id in done:
        del futures[rpc_id]


def _cancel_all_rpcs(futures: Mapping[int, grpc.Future]) -> None:
    logger.info("Cancelling all remaining RPCs")
    for future in futures.values():
        future.cancel()


def _run_single_channel(args: argparse.Namespace):
    global _global_rpc_id  # pylint: disable=global-statement
    duration_per_query = 1.0 / float(args.qps)
    with grpc.insecure_channel(args.server) as channel:
        stub = test_pb2_grpc.TestServiceStub(channel)
        futures: Dict[int, grpc.Future] = {}
        while not _stop_event.is_set():
            request_id = None
            with _global_lock:
                request_id = _global_rpc_id
                _global_rpc_id += 1
            start = time.time()
            end = start + duration_per_query
            _start_rpc(request_id, stub, float(args.rpc_timeout_sec), futures)
            _remove_completed_rpcs(futures, args.print_response)
            logger.debug(f"Currently {len(futures)} in-flight RPCs")
            now = time.time()
            while now < end:
                time.sleep(end - now)
                now = time.time()
        _cancel_all_rpcs(futures)


def _run(args: argparse.Namespace) -> None:
    logger.info("Starting python xDS Interop Client.")
    global _global_server  # pylint: disable=global-statement
    channel_threads: List[threading.Thread] = []
    for i in range(args.num_channels):
        thread = threading.Thread(target=_run_single_channel, args=(args,))
        thread.start()
        channel_threads.append(thread)
    _global_server = grpc.server(futures.ThreadPoolExecutor())
    _global_server.add_insecure_port(f"0.0.0.0:{args.stats_port}")
    test_pb2_grpc.add_LoadBalancerStatsServiceServicer_to_server(
        _LoadBalancerStatsServicer(), _global_server)
    _global_server.start()
    _global_server.wait_for_termination()
    for i in range(args.num_channels):
        thread.join()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description='Run Python XDS interop client.')
    parser.add_argument(
        "--num_channels",
        default=1,
        type=int,
        help="The number of channels from which to send requests.")
    parser.add_argument("--print_response",
                        default=False,
                        action="store_true",
                        help="Write RPC response to STDOUT.")
    parser.add_argument(
        "--qps",
        default=1,
        type=int,
        help="The number of queries to send from each channel per second.")
    parser.add_argument("--rpc_timeout_sec",
                        default=30,
                        type=int,
                        help="The per-RPC timeout in seconds.")
    parser.add_argument("--server",
                        default="localhost:50051",
                        help="The address of the server.")
    parser.add_argument(
        "--stats_port",
        default=50052,
        type=int,
        help="The port on which to expose the peer distribution stats service.")
    parser.add_argument('--verbose',
                        help='verbose log output',
                        default=False,
                        action='store_true')
    parser.add_argument("--log_file",
                        default=None,
                        type=str,
                        help="A file to log to.")
    args = parser.parse_args()
    signal.signal(signal.SIGINT, _handle_sigint)
    if args.verbose:
        logger.setLevel(logging.DEBUG)
    if args.log_file:
        file_handler = logging.FileHandler(args.log_file, mode='a')
        file_handler.setFormatter(formatter)
        logger.addHandler(file_handler)
    _run(args)
