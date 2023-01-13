# Copyright 2017 gRPC authors.
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
"""Common interfaces and implementation."""

import abc
import collections
from typing import Any, Callable, List, Mapping, Optional, Tuple, Union

from google.protobuf import descriptor  # pytype: disable=pyi-error
import grpc
from grpc._typing import MetadataType
from grpc._typing import TuplifiedMetadataType


def _fuss(
        tuplified_metadata: Union[TuplifiedMetadataType,
                                  Tuple]) -> MetadataType:
    return tuplified_metadata + ((
        'grpc.metadata_added_by_runtime',
        'gRPC is allowed to add metadata in transmission and does so.',
    ),)


FUSSED_EMPTY_METADATA = _fuss(())


def fuss_with_metadata(metadata: Optional[MetadataType]) -> MetadataType:
    if metadata is None:
        return FUSSED_EMPTY_METADATA
    else:
        return _fuss(tuple(metadata))


def rpc_names(
    service_descriptors: descriptor.ServiceDescriptor
) -> Mapping[str, descriptor.ServiceDescriptor]:
    rpc_names_to_descriptors = {}
    for service_descriptor in service_descriptors:
        for method_descriptor in service_descriptor.methods_by_name.values():
            rpc_name = '/{}/{}'.format(service_descriptor.full_name,
                                       method_descriptor.name)
            rpc_names_to_descriptors[rpc_name] = method_descriptor
    return rpc_names_to_descriptors


class ChannelRpcRead(
        collections.namedtuple('ChannelRpcRead', (
            'response',
            'trailing_metadata',
            'code',
            'details',
        ))):
    pass


class ChannelRpcHandler(abc.ABC):

    @abc.abstractmethod
    def initial_metadata(self) -> Optional[MetadataType]:
        raise NotImplementedError()

    @abc.abstractmethod
    def add_request(self, request: Any) -> bool:
        raise NotImplementedError()

    @abc.abstractmethod
    def close_requests(self) -> None:
        raise NotImplementedError()

    @abc.abstractmethod
    def take_response(self) -> ChannelRpcRead:
        raise NotImplementedError()

    @abc.abstractmethod
    def cancel(self, code: grpc.StatusCode, details: str) -> bool:
        raise NotImplementedError()

    @abc.abstractmethod
    def termination(self) -> Tuple[MetadataType, grpc.StatusCode, str]:
        raise NotImplementedError()

    @abc.abstractmethod
    def is_active(self) -> bool:
        raise NotImplementedError()

    @abc.abstractmethod
    def time_remaining(self) -> float:
        raise NotImplementedError()

    @abc.abstractmethod
    def add_callback(self, callback: Callable) -> None:
        raise NotImplementedError()


class ChannelHandler(abc.ABC):

    @abc.abstractmethod
    def invoke_rpc(self, method_full_rpc_name: str,
                   invocation_metadata: MetadataType, requests: List,
                   requests_closed: bool,
                   timeout: Optional[float]) -> ChannelRpcHandler:
        raise NotImplementedError()


class ServerRpcRead(
        collections.namedtuple('ServerRpcRead', (
            'request',
            'requests_closed',
            'terminated',
        ))):
    pass


REQUESTS_CLOSED = ServerRpcRead(None, True, False)
TERMINATED = ServerRpcRead(None, False, True)


class ServerRpcHandler(abc.ABC):

    @abc.abstractmethod
    def send_initial_metadata(self, initial_metadata: MetadataType) -> None:
        raise NotImplementedError()

    @abc.abstractmethod
    def take_request(self) -> ServerRpcRead:
        raise NotImplementedError()

    @abc.abstractmethod
    def add_response(self, response: Any) -> None:
        raise NotImplementedError()

    @abc.abstractmethod
    def send_termination(self, trailing_metadata: Optional[MetadataType],
                         code: grpc.StatusCode, details: str) -> None:
        raise NotImplementedError()

    @abc.abstractmethod
    def add_termination_callback(self, callback: Callable[[], None]) -> bool:
        raise NotImplementedError()


class Serverish(abc.ABC):

    @abc.abstractmethod
    def invoke_unary_unary(self, method_descriptor, handler,
                           invocation_metadata, request, deadline):
        raise NotImplementedError()

    @abc.abstractmethod
    def invoke_unary_stream(self, method_descriptor, handler,
                            invocation_metadata, request, deadline):
        raise NotImplementedError()

    @abc.abstractmethod
    def invoke_stream_unary(self, method_descriptor, handler,
                            invocation_metadata, deadline):
        raise NotImplementedError()

    @abc.abstractmethod
    def invoke_stream_stream(self, method_descriptor, handler,
                             invocation_metadata, deadline):
        raise NotImplementedError()
