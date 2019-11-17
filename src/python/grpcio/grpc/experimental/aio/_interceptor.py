# Copyright 2019 gRPC authors.
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
"""Interceptors implementation of gRPC Asyncio Python."""
from typing import Callable

import grpc


class UnaryUnaryClientInterceptor:
    """Affords intercepting unary-unary invocations."""

    async def intercept_unary_unary(
            self,
            continuation: Callable[[grpc.ClientCallDetails, bytes], bytes],
            client_call_details: grpc.ClientCallDetails,
            request: bytes) -> bytes:
        """Intercepts a unary-unary invocation asynchronously.
        Args:
          continuation: A coroutine that proceeds with the invocation by
            executing the next interceptor in chain or invoking the
            actual RPC on the underlying Channel. It is the interceptor's
            responsibility to call it if it decides to move the RPC forward.
            The interceptor can use
            `response_future = await continuation(client_call_details, request)`
            to continue with the RPC. `continuation` returns the response of the
            RPC.
          client_call_details: A ClientCallDetails object describing the
            outgoing RPC.
          request: The request value for the RPC.
        Returns:
            An object with the RPC response.
        Raises:
          AioRpcError: Indicating that the RPC terminated with non-OK status.
          asyncio.CancelledError: Indicating that the RPC was canceled.
        """
