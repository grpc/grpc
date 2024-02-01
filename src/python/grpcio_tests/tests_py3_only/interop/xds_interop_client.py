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
import collections
import concurrent.futures
import datetime
import logging
import signal
import threading
import time
from typing import (
    DefaultDict,
    Dict,
    FrozenSet,
    Iterable,
    List,
    Mapping,
    Sequence,
    Set,
    Tuple,
)

import grpc
from grpc import _typing as grpc_typing
import grpc_admin
from grpc_channelz.v1 import channelz

from src.proto.grpc.testing import empty_pb2
from src.proto.grpc.testing import messages_pb2
from src.proto.grpc.testing import test_pb2
from src.proto.grpc.testing import test_pb2_grpc

logger = logging.getLogger()
console_handler = logging.StreamHandler()
formatter = logging.Formatter(fmt="%(asctime)s: %(levelname)-8s %(message)s")
console_handler.setFormatter(formatter)
logger.addHandler(console_handler)

_SUPPORTED_METHODS = (
    "UnaryCall",
    "EmptyCall",
)

_METHOD_CAMEL_TO_CAPS_SNAKE = {
    "UnaryCall": "UNARY_CALL",
    "EmptyCall": "EMPTY_CALL",
}

_METHOD_STR_TO_ENUM = {
    "UnaryCall": messages_pb2.ClientConfigureRequest.UNARY_CALL,
    "EmptyCall": messages_pb2.ClientConfigureRequest.EMPTY_CALL,
}

_METHOD_ENUM_TO_STR = {v: k for k, v in _METHOD_STR_TO_ENUM.items()}

PerMethodMetadataType = Mapping[str, Sequence[Tuple[str, str]]]


# FutureFromCall is both a grpc.Call and grpc.Future
class FutureFromCallType(grpc.Call, grpc.Future):
    pass


_CONFIG_CHANGE_TIMEOUT = datetime.timedelta(milliseconds=500)


class _StatsWatcher:
    _start: int
    _end: int
    _rpcs_needed: int
    _rpcs_by_peer: DefaultDict[str, int]
    _rpcs_by_method: DefaultDict[str, DefaultDict[str, int]]
    _no_remote_peer: int
    _lock: threading.Lock
    _condition: threading.Condition
    _metadata_keys: FrozenSet[str]
    _include_all_metadata: bool
    _metadata_by_peer: DefaultDict[
        str, messages_pb2.LoadBalancerStatsResponse.MetadataByPeer
    ]

    def __init__(self, start: int, end: int, metadata_keys: Iterable[str]):
        self._start = start
        self._end = end
        self._rpcs_needed = end - start
        self._rpcs_by_peer = collections.defaultdict(int)
        self._rpcs_by_method = collections.defaultdict(
            lambda: collections.defaultdict(int)
        )
        self._condition = threading.Condition()
        self._no_remote_peer = 0
        self._metadata_keys = frozenset(
            self._sanitize_metadata_key(key) for key in metadata_keys
        )
        self._include_all_metadata = "*" in self._metadata_keys
        self._metadata_by_peer = collections.defaultdict(
            messages_pb2.LoadBalancerStatsResponse.MetadataByPeer
        )

    @classmethod
    def _sanitize_metadata_key(cls, metadata_key: str) -> str:
        return metadata_key.strip().lower()

    def _add_metadata(
        self,
        rpc_metadata: messages_pb2.LoadBalancerStatsResponse.RpcMetadata,
        metadata_to_add: grpc_typing.MetadataType,
        metadata_type: messages_pb2.LoadBalancerStatsResponse.MetadataType,
    ) -> None:
        for key, value in metadata_to_add:
            if (
                self._include_all_metadata
                or self._sanitize_metadata_key(key) in self._metadata_keys
            ):
                rpc_metadata.metadata.append(
                    messages_pb2.LoadBalancerStatsResponse.MetadataEntry(
                        key=key, value=value, type=metadata_type
                    )
                )

    def on_rpc_complete(
        self,
        request_id: int,
        peer: str,
        method: str,
        *,
        initial_metadata: grpc_typing.MetadataType,
        trailing_metadata: grpc_typing.MetadataType,
    ) -> None:
        """Records statistics for a single RPC."""
        if self._start <= request_id < self._end:
            with self._condition:
                if not peer:
                    self._no_remote_peer += 1
                else:
                    self._rpcs_by_peer[peer] += 1
                    self._rpcs_by_method[method][peer] += 1
                    if self._metadata_keys:
                        rpc_metadata = (
                            messages_pb2.LoadBalancerStatsResponse.RpcMetadata()
                        )
                        self._add_metadata(
                            rpc_metadata,
                            initial_metadata,
                            messages_pb2.LoadBalancerStatsResponse.MetadataType.INITIAL,
                        )
                        self._add_metadata(
                            rpc_metadata,
                            trailing_metadata,
                            messages_pb2.LoadBalancerStatsResponse.MetadataType.TRAILING,
                        )
                        self._metadata_by_peer[peer].rpc_metadata.append(
                            rpc_metadata
                        )
                self._rpcs_needed -= 1
                self._condition.notify()

    def await_rpc_stats_response(
        self, timeout_sec: int
    ) -> messages_pb2.LoadBalancerStatsResponse:
        """Blocks until a full response has been collected."""
        with self._condition:
            self._condition.wait_for(
                lambda: not self._rpcs_needed, timeout=float(timeout_sec)
            )
            response = messages_pb2.LoadBalancerStatsResponse()
            for peer, count in self._rpcs_by_peer.items():
                response.rpcs_by_peer[peer] = count
            for method, count_by_peer in self._rpcs_by_method.items():
                for peer, count in count_by_peer.items():
                    response.rpcs_by_method[method].rpcs_by_peer[peer] = count
            for peer, metadata_by_peer in self._metadata_by_peer.items():
                response.metadatas_by_peer[peer].CopyFrom(metadata_by_peer)
            response.num_failures = self._no_remote_peer + self._rpcs_needed
        return response


_global_lock = threading.Lock()
_stop_event = threading.Event()
_global_rpc_id: int = 0
_watchers: Set[_StatsWatcher] = set()
_global_server = None
_global_rpcs_started: Mapping[str, int] = collections.defaultdict(int)
_global_rpcs_succeeded: Mapping[str, int] = collections.defaultdict(int)
_global_rpcs_failed: Mapping[str, int] = collections.defaultdict(int)

# Mapping[method, Mapping[status_code, count]]
_global_rpc_statuses: Mapping[str, Mapping[int, int]] = collections.defaultdict(
    lambda: collections.defaultdict(int)
)


def _handle_sigint(sig, frame) -> None:
    logger.warning("Received SIGINT")
    _stop_event.set()
    _global_server.stop(None)


class _LoadBalancerStatsServicer(
    test_pb2_grpc.LoadBalancerStatsServiceServicer
):
    def __init__(self):
        super(_LoadBalancerStatsServicer).__init__()

    def GetClientStats(
        self,
        request: messages_pb2.LoadBalancerStatsRequest,
        context: grpc.ServicerContext,
    ) -> messages_pb2.LoadBalancerStatsResponse:
        logger.info("Received stats request.")
        start = None
        end = None
        watcher = None
        with _global_lock:
            start = _global_rpc_id + 1
            end = start + request.num_rpcs
            watcher = _StatsWatcher(start, end, request.metadata_keys)
            _watchers.add(watcher)
        response = watcher.await_rpc_stats_response(request.timeout_sec)
        with _global_lock:
            _watchers.remove(watcher)
        logger.info("Returning stats response: %s", response)
        return response

    def GetClientAccumulatedStats(
        self,
        request: messages_pb2.LoadBalancerAccumulatedStatsRequest,
        context: grpc.ServicerContext,
    ) -> messages_pb2.LoadBalancerAccumulatedStatsResponse:
        logger.info("Received cumulative stats request.")
        response = messages_pb2.LoadBalancerAccumulatedStatsResponse()
        with _global_lock:
            for method in _SUPPORTED_METHODS:
                caps_method = _METHOD_CAMEL_TO_CAPS_SNAKE[method]
                response.num_rpcs_started_by_method[
                    caps_method
                ] = _global_rpcs_started[method]
                response.num_rpcs_succeeded_by_method[
                    caps_method
                ] = _global_rpcs_succeeded[method]
                response.num_rpcs_failed_by_method[
                    caps_method
                ] = _global_rpcs_failed[method]
                response.stats_per_method[
                    caps_method
                ].rpcs_started = _global_rpcs_started[method]
                for code, count in _global_rpc_statuses[method].items():
                    response.stats_per_method[caps_method].result[code] = count
        logger.info("Returning cumulative stats response.")
        return response


def _start_rpc(
    method: str,
    metadata: Sequence[Tuple[str, str]],
    request_id: int,
    stub: test_pb2_grpc.TestServiceStub,
    timeout: float,
    futures: Mapping[int, Tuple[FutureFromCallType, str]],
) -> None:
    logger.debug(f"Sending {method} request to backend: {request_id}")
    if method == "UnaryCall":
        future = stub.UnaryCall.future(
            messages_pb2.SimpleRequest(), metadata=metadata, timeout=timeout
        )
    elif method == "EmptyCall":
        future = stub.EmptyCall.future(
            empty_pb2.Empty(), metadata=metadata, timeout=timeout
        )
    else:
        raise ValueError(f"Unrecognized method '{method}'.")
    futures[request_id] = (future, method)


def _on_rpc_done(
    rpc_id: int, future: FutureFromCallType, method: str, print_response: bool
) -> None:
    exception = future.exception()
    hostname = ""
    with _global_lock:
        _global_rpc_statuses[method][future.code().value[0]] += 1
    if exception is not None:
        with _global_lock:
            _global_rpcs_failed[method] += 1
        if exception.code() == grpc.StatusCode.DEADLINE_EXCEEDED:
            logger.error(f"RPC {rpc_id} timed out")
        else:
            logger.error(exception)
    else:
        response = future.result()
        hostname = None
        for metadatum in future.initial_metadata():
            if metadatum[0] == "hostname":
                hostname = metadatum[1]
                break
        else:
            hostname = response.hostname
        if future.code() == grpc.StatusCode.OK:
            with _global_lock:
                _global_rpcs_succeeded[method] += 1
        else:
            with _global_lock:
                _global_rpcs_failed[method] += 1
        if print_response:
            if future.code() == grpc.StatusCode.OK:
                logger.debug("Successful response.")
            else:
                logger.debug(f"RPC failed: {rpc_id}")
    with _global_lock:
        for watcher in _watchers:
            watcher.on_rpc_complete(
                rpc_id,
                hostname,
                method,
                initial_metadata=future.initial_metadata(),
                trailing_metadata=future.trailing_metadata(),
            )


def _remove_completed_rpcs(
    rpc_futures: Mapping[int, FutureFromCallType], print_response: bool
) -> None:
    logger.debug("Removing completed RPCs")
    done = []
    for future_id, (future, method) in rpc_futures.items():
        if future.done():
            _on_rpc_done(future_id, future, method, args.print_response)
            done.append(future_id)
    for rpc_id in done:
        del rpc_futures[rpc_id]


def _cancel_all_rpcs(futures: Mapping[int, Tuple[grpc.Future, str]]) -> None:
    logger.info("Cancelling all remaining RPCs")
    for future, _ in futures.values():
        future.cancel()


class _ChannelConfiguration:
    """Configuration for a single client channel.

    Instances of this class are meant to be dealt with as PODs. That is,
    data member should be accessed directly. This class is not thread-safe.
    When accessing any of its members, the lock member should be held.
    """

    def __init__(
        self,
        method: str,
        metadata: Sequence[Tuple[str, str]],
        qps: int,
        server: str,
        rpc_timeout_sec: int,
        print_response: bool,
        secure_mode: bool,
    ):
        # condition is signalled when a change is made to the config.
        self.condition = threading.Condition()

        self.method = method
        self.metadata = metadata
        self.qps = qps
        self.server = server
        self.rpc_timeout_sec = rpc_timeout_sec
        self.print_response = print_response
        self.secure_mode = secure_mode


def _run_single_channel(config: _ChannelConfiguration) -> None:
    global _global_rpc_id  # pylint: disable=global-statement
    with config.condition:
        server = config.server
    channel = None
    if config.secure_mode:
        fallback_creds = grpc.experimental.insecure_channel_credentials()
        channel_creds = grpc.xds_channel_credentials(fallback_creds)
        channel = grpc.secure_channel(server, channel_creds)
    else:
        channel = grpc.insecure_channel(server)
    with channel:
        stub = test_pb2_grpc.TestServiceStub(channel)
        futures: Dict[int, Tuple[FutureFromCallType, str]] = {}
        while not _stop_event.is_set():
            with config.condition:
                if config.qps == 0:
                    config.condition.wait(
                        timeout=_CONFIG_CHANGE_TIMEOUT.total_seconds()
                    )
                    continue
                else:
                    duration_per_query = 1.0 / float(config.qps)
                request_id = None
                with _global_lock:
                    request_id = _global_rpc_id
                    _global_rpc_id += 1
                    _global_rpcs_started[config.method] += 1
                start = time.time()
                end = start + duration_per_query
                _start_rpc(
                    config.method,
                    config.metadata,
                    request_id,
                    stub,
                    float(config.rpc_timeout_sec),
                    futures,
                )
                print_response = config.print_response
            _remove_completed_rpcs(futures, config.print_response)
            logger.debug(f"Currently {len(futures)} in-flight RPCs")
            now = time.time()
            while now < end:
                time.sleep(end - now)
                now = time.time()
        _cancel_all_rpcs(futures)


class _XdsUpdateClientConfigureServicer(
    test_pb2_grpc.XdsUpdateClientConfigureServiceServicer
):
    def __init__(
        self, per_method_configs: Mapping[str, _ChannelConfiguration], qps: int
    ):
        super(_XdsUpdateClientConfigureServicer).__init__()
        self._per_method_configs = per_method_configs
        self._qps = qps

    def Configure(
        self,
        request: messages_pb2.ClientConfigureRequest,
        context: grpc.ServicerContext,
    ) -> messages_pb2.ClientConfigureResponse:
        logger.info("Received Configure RPC: %s", request)
        method_strs = [_METHOD_ENUM_TO_STR[t] for t in request.types]
        for method in _SUPPORTED_METHODS:
            method_enum = _METHOD_STR_TO_ENUM[method]
            channel_config = self._per_method_configs[method]
            if method in method_strs:
                qps = self._qps
                metadata = (
                    (md.key, md.value)
                    for md in request.metadata
                    if md.type == method_enum
                )
                # For backward compatibility, do not change timeout when we
                # receive a default value timeout.
                if request.timeout_sec == 0:
                    timeout_sec = channel_config.rpc_timeout_sec
                else:
                    timeout_sec = request.timeout_sec
            else:
                qps = 0
                metadata = ()
                # Leave timeout unchanged for backward compatibility.
                timeout_sec = channel_config.rpc_timeout_sec
            with channel_config.condition:
                channel_config.qps = qps
                channel_config.metadata = list(metadata)
                channel_config.rpc_timeout_sec = timeout_sec
                channel_config.condition.notify_all()
        return messages_pb2.ClientConfigureResponse()


class _MethodHandle:
    """An object grouping together threads driving RPCs for a method."""

    _channel_threads: List[threading.Thread]

    def __init__(
        self, num_channels: int, channel_config: _ChannelConfiguration
    ):
        """Creates and starts a group of threads running the indicated method."""
        self._channel_threads = []
        for i in range(num_channels):
            thread = threading.Thread(
                target=_run_single_channel, args=(channel_config,)
            )
            thread.start()
            self._channel_threads.append(thread)

    def stop(self) -> None:
        """Joins all threads referenced by the handle."""
        for channel_thread in self._channel_threads:
            channel_thread.join()


def _run(
    args: argparse.Namespace,
    methods: Sequence[str],
    per_method_metadata: PerMethodMetadataType,
) -> None:
    logger.info("Starting python xDS Interop Client.")
    global _global_server  # pylint: disable=global-statement
    method_handles = []
    channel_configs = {}
    for method in _SUPPORTED_METHODS:
        if method in methods:
            qps = args.qps
        else:
            qps = 0
        channel_config = _ChannelConfiguration(
            method,
            per_method_metadata.get(method, []),
            qps,
            args.server,
            args.rpc_timeout_sec,
            args.print_response,
            args.secure_mode,
        )
        channel_configs[method] = channel_config
        method_handles.append(_MethodHandle(args.num_channels, channel_config))
    _global_server = grpc.server(concurrent.futures.ThreadPoolExecutor())
    _global_server.add_insecure_port(f"0.0.0.0:{args.stats_port}")
    test_pb2_grpc.add_LoadBalancerStatsServiceServicer_to_server(
        _LoadBalancerStatsServicer(), _global_server
    )
    test_pb2_grpc.add_XdsUpdateClientConfigureServiceServicer_to_server(
        _XdsUpdateClientConfigureServicer(channel_configs, args.qps),
        _global_server,
    )
    channelz.add_channelz_servicer(_global_server)
    grpc_admin.add_admin_servicers(_global_server)
    _global_server.start()
    _global_server.wait_for_termination()
    for method_handle in method_handles:
        method_handle.stop()


def parse_metadata_arg(metadata_arg: str) -> PerMethodMetadataType:
    metadata = metadata_arg.split(",") if args.metadata else []
    per_method_metadata = collections.defaultdict(list)
    for metadatum in metadata:
        elems = metadatum.split(":")
        if len(elems) != 3:
            raise ValueError(
                f"'{metadatum}' was not in the form 'METHOD:KEY:VALUE'"
            )
        if elems[0] not in _SUPPORTED_METHODS:
            raise ValueError(f"Unrecognized method '{elems[0]}'")
        per_method_metadata[elems[0]].append((elems[1], elems[2]))
    return per_method_metadata


def parse_rpc_arg(rpc_arg: str) -> Sequence[str]:
    methods = rpc_arg.split(",")
    if set(methods) - set(_SUPPORTED_METHODS):
        raise ValueError(
            "--rpc supported methods: {}".format(", ".join(_SUPPORTED_METHODS))
        )
    return methods


def bool_arg(arg: str) -> bool:
    if arg.lower() in ("true", "yes", "y"):
        return True
    elif arg.lower() in ("false", "no", "n"):
        return False
    else:
        raise argparse.ArgumentTypeError(f"Could not parse '{arg}' as a bool.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Run Python XDS interop client."
    )
    parser.add_argument(
        "--num_channels",
        default=1,
        type=int,
        help="The number of channels from which to send requests.",
    )
    parser.add_argument(
        "--print_response",
        default="False",
        type=bool_arg,
        help="Write RPC response to STDOUT.",
    )
    parser.add_argument(
        "--qps",
        default=1,
        type=int,
        help="The number of queries to send from each channel per second.",
    )
    parser.add_argument(
        "--rpc_timeout_sec",
        default=30,
        type=int,
        help="The per-RPC timeout in seconds.",
    )
    parser.add_argument(
        "--server", default="localhost:50051", help="The address of the server."
    )
    parser.add_argument(
        "--stats_port",
        default=50052,
        type=int,
        help="The port on which to expose the peer distribution stats service.",
    )
    parser.add_argument(
        "--secure_mode",
        default="False",
        type=bool_arg,
        help="If specified, uses xDS credentials to connect to the server.",
    )
    parser.add_argument(
        "--verbose",
        help="verbose log output",
        default=False,
        action="store_true",
    )
    parser.add_argument(
        "--log_file", default=None, type=str, help="A file to log to."
    )
    rpc_help = "A comma-delimited list of RPC methods to run. Must be one of "
    rpc_help += ", ".join(_SUPPORTED_METHODS)
    rpc_help += "."
    parser.add_argument("--rpc", default="UnaryCall", type=str, help=rpc_help)
    metadata_help = (
        "A comma-delimited list of 3-tuples of the form "
        + "METHOD:KEY:VALUE, e.g. "
        + "EmptyCall:key1:value1,UnaryCall:key2:value2,EmptyCall:k3:v3"
    )
    parser.add_argument("--metadata", default="", type=str, help=metadata_help)
    args = parser.parse_args()
    signal.signal(signal.SIGINT, _handle_sigint)
    if args.verbose:
        logger.setLevel(logging.DEBUG)
    if args.log_file:
        file_handler = logging.FileHandler(args.log_file, mode="a")
        file_handler.setFormatter(formatter)
        logger.addHandler(file_handler)
    _run(args, parse_rpc_arg(args.rpc), parse_metadata_arg(args.metadata))
