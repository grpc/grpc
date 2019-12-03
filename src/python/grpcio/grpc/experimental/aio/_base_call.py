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
"""Abstract base classes for client-side Call objects.

Call objects represents the RPC itself, and offer methods to access / modify
its information. They also offer methods to manipulate the life-cycle of the
RPC, e.g. cancellation.
"""

from abc import ABCMeta, abstractmethod
from typing import AsyncIterable, Awaitable, Generic, Text

import grpc

from ._typing import MetadataType, RequestType, ResponseType

__all__ = 'Call', 'UnaryUnaryCall', 'UnaryStreamCall'


class Call(grpc.RpcContext, metaclass=ABCMeta):
    """The abstract base class of an RPC on the client-side."""

    @abstractmethod
    def cancelled(self) -> bool:
        """Return True if the RPC is cancelled.

        The RPC is cancelled when the cancellation was requested with cancel().

        Returns:
          A bool indicates whether the RPC is cancelled or not.
        """

    @abstractmethod
    def done(self) -> bool:
        """Return True if the RPC is done.

        An RPC is done if the RPC is completed, cancelled or aborted.

        Returns:
          A bool indicates if the RPC is done.
        """

    @abstractmethod
    async def initial_metadata(self) -> MetadataType:
        """Accesses the initial metadata sent by the server.

        Coroutine continues once the value is available.

        Returns:
          The initial :term:`metadata`.
        """

    @abstractmethod
    async def trailing_metadata(self) -> MetadataType:
        """Accesses the trailing metadata sent by the server.

        Coroutine continues once the value is available.

        Returns:
          The trailing :term:`metadata`.
        """

    @abstractmethod
    async def code(self) -> grpc.StatusCode:
        """Accesses the status code sent by the server.

        Coroutine continues once the value is available.

        Returns:
          The StatusCode value for the RPC.
        """

    @abstractmethod
    async def details(self) -> Text:
        """Accesses the details sent by the server.

        Coroutine continues once the value is available.

        Returns:
          The details string of the RPC.
        """


class UnaryUnaryCall(
        Generic[RequestType, ResponseType], Call, metaclass=ABCMeta):
    """The abstract base class of an unary-unary RPC on the client-side."""

    @abstractmethod
    def __await__(self) -> Awaitable[ResponseType]:
        """Await the response message to be ready.

        Returns:
          The response message of the RPC.
        """


class UnaryStreamCall(
        Generic[RequestType, ResponseType], Call, metaclass=ABCMeta):

    @abstractmethod
    def __aiter__(self) -> AsyncIterable[ResponseType]:
        """Returns the async iterable representation that yields messages.

        Under the hood, it is calling the "read" method.

        Returns:
          An async iterable object that yields messages.
        """

    @abstractmethod
    async def read(self) -> ResponseType:
        """Reads one message from the RPC.

        Parallel read operations is not allowed.

        Returns:
          A response message of the RPC.
        """
