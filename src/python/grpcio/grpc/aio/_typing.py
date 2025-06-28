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
"""Common types for gRPC Async API"""
from collections.abc import AsyncIterable
from collections.abc import Iterable
from collections.abc import Sequence
from typing import Callable, Tuple, TypeVar, Union
from __future__ import annotations

from typing import (
    Any,
    AsyncIterable,
    Callable,
    Iterable,
    Sequence,
    Tuple,
    TypeVar,
    Union,
)

from grpc._cython.cygrpc import EOF

from ._metadata import Metadata
from ._metadata import MetadataKey
from ._metadata import MetadataValue

RequestType = TypeVar("RequestType")
ResponseType = TypeVar("ResponseType")
T = TypeVar("T")
SerializingFunction = Callable[[T], bytes]
DeserializingFunction = Callable[[bytes], T]
MetadatumType = Tuple[MetadataKey, MetadataValue]
MetadataType = Union[Metadata, Sequence[MetadatumType]]
ChannelArgumentType = Tuple[Union[str, bytes], Union[str, bytes, int]]
ChannelArgsType = Union[
    Sequence[ChannelArgumentType], Tuple[ChannelArgumentType, ...]
]
EOFType = type(EOF)
DoneCallbackType = Callable[[], None]
RequestIterableType = Union[Iterable[RequestType], AsyncIterable[RequestType]]
ResponseIterableType = AsyncIterable[ResponseType]
