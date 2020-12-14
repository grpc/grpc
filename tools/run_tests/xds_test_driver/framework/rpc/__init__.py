# Copyright 2020 gRPC authors.
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
import re
from typing import Optional, ClassVar, Dict

import grpc
from google.protobuf import json_format
import google.protobuf.message

logger = logging.getLogger(__name__)

# Type aliases
Message = google.protobuf.message.Message


class GrpcClientHelper:
    channel: grpc.Channel
    DEFAULT_CONNECTION_TIMEOUT_SEC = 60
    DEFAULT_WAIT_FOR_READY_SEC = 60

    def __init__(self, channel: grpc.Channel, stub_class: ClassVar):
        self.channel = channel
        self.stub = stub_class(channel)
        # For better logging
        self.service_name = re.sub('Stub$', '', self.stub.__class__.__name__)

    def call_unary_when_channel_ready(
            self,
            *,
            rpc: str,
            req: Message,
            wait_for_ready_sec: Optional[int] = DEFAULT_WAIT_FOR_READY_SEC,
            connection_timeout_sec: Optional[
                int] = DEFAULT_CONNECTION_TIMEOUT_SEC) -> Message:
        if wait_for_ready_sec is None:
            wait_for_ready_sec = self.DEFAULT_WAIT_FOR_READY_SEC
        if connection_timeout_sec is None:
            connection_timeout_sec = self.DEFAULT_CONNECTION_TIMEOUT_SEC

        timeout_sec = wait_for_ready_sec + connection_timeout_sec
        rpc_callable: grpc.UnaryUnaryMultiCallable = getattr(self.stub, rpc)

        call_kwargs = dict(wait_for_ready=True, timeout=timeout_sec)
        self._log_debug(rpc, req, call_kwargs)
        return rpc_callable(req, **call_kwargs)

    def _log_debug(self, rpc, req, call_kwargs):
        logger.debug('RPC %s.%s(request=%s(%r), %s)', self.service_name, rpc,
                     req.__class__.__name__, json_format.MessageToDict(req),
                     ', '.join({f'{k}={v}' for k, v in call_kwargs.items()}))


class GrpcApp:
    channels: Dict[int, grpc.Channel]

    class NotFound(Exception):
        """Requested resource not found"""

    def __init__(self, rpc_host):
        self.rpc_host = rpc_host
        # Cache gRPC channels per port
        self.channels = dict()

    def _make_channel(self, port) -> grpc.Channel:
        if port not in self.channels:
            target = f'{self.rpc_host}:{port}'
            self.channels[port] = grpc.insecure_channel(target)
        return self.channels[port]

    def close(self):
        # Close all channels
        for channel in self.channels.values():
            channel.close()

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()
        return False

    def __del__(self):
        self.close()
