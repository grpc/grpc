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
from typing import (
    Any,
    Callable,
    TypeVar,
    Union,
)

from grpc._cython.cygrpc import EOF

from ._metadata import Metadata
from ._metadata import MetadataKey
from ._metadata import MetadataValue

RequestType = TypeVar("RequestType")
ResponseType = TypeVar("ResponseType")
SerializingFunction = Callable[[Any], bytes]
DeserializingFunction = Callable[[bytes], Any]
MetadatumType = tuple[MetadataKey, MetadataValue]
MetadataType = Union[Metadata, Sequence[MetadatumType]]
ChannelArgumentType = Sequence[tuple[str, Any]]
EOFType = type(EOF)
DoneCallbackType = Callable[[Any], None]
RequestIterableType = Union[Iterable[Any], AsyncIterable[Any]]
ResponseIterableType = AsyncIterable[Any]
