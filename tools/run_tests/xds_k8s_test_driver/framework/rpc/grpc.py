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
from typing import Any, Dict, Optional

from google.protobuf import json_format
import google.protobuf.message
import grpc

logger = logging.getLogger(__name__)

# Type aliases
Message = google.protobuf.message.Message


class GrpcClientHelper:
    DEFAULT_RPC_DEADLINE_SEC = 90
    channel: grpc.Channel
    # This is purely cosmetic to make RPC logs look like method calls.
    log_service_name: str
    # This is purely cosmetic to output the RPC target. Normally set to the
    # hostname:port of the remote service, but it doesn't have to be the
    # real target. This is done so that when RPC are routed to the proxy
    # or port forwarding, this still is set to a useful name.
    log_target: str

    def __init__(self,
                 channel: grpc.Channel,
                 stub_class: Any,
                 *,
                 log_target: Optional[str] = ''):
        self.channel = channel
        self.stub = stub_class(channel)
        self.log_service_name = re.sub('Stub$', '',
                                       self.stub.__class__.__name__)
        self.log_target = log_target or ''

    def call_unary_with_deadline(
            self,
            *,
            rpc: str,
            req: Message,
            deadline_sec: Optional[int] = DEFAULT_RPC_DEADLINE_SEC,
            log_level: Optional[int] = logging.DEBUG) -> Message:
        if deadline_sec is None:
            deadline_sec = self.DEFAULT_RPC_DEADLINE_SEC

        call_kwargs = dict(wait_for_ready=True, timeout=deadline_sec)
        self._log_rpc_request(rpc, req, call_kwargs, log_level)

        # Call RPC, e.g. RpcStub(channel).RpcMethod(req, ...options)
        rpc_callable: grpc.UnaryUnaryMultiCallable = getattr(self.stub, rpc)
        return rpc_callable(req, **call_kwargs)

    def _log_rpc_request(self, rpc, req, call_kwargs, log_level=logging.DEBUG):
        logger.log(logging.DEBUG if log_level is None else log_level,
                   '[%s] >> RPC %s.%s(request=%s(%r), %s)', self.log_target,
                   self.log_service_name, rpc, req.__class__.__name__,
                   json_format.MessageToDict(req),
                   ', '.join({f'{k}={v}' for k, v in call_kwargs.items()}))


class GrpcApp:
    channels: Dict[int, grpc.Channel]

    class NotFound(Exception):
        """Requested resource not found"""

        def __init__(self, message):
            self.message = message
            super().__init__(message)

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
