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

from typing import (
    Any,
    AsyncIterable,
    Callable,
    Iterable,
    Sequence,
    Tuple,
    TypeAlias,
    TypeVar,
    Union,
)

from grpc._cython.cygrpc import _EOF

# pylint: disable=unused-import
from ._metadata import Metadata
from ._metadata import MetadataKey
from ._metadata import MetadataType
from ._metadata import MetadataValue
from ._metadata import MetadatumType

# pylint: enable=unused-import

RequestType = TypeVar("RequestType")
ResponseType = TypeVar("ResponseType")

SerializerInputType_contra = TypeVar(
    "SerializerInputType_contra", contravariant=True
)
DeserializerOutputType_co = TypeVar("DeserializerOutputType_co", covariant=True)
SerializingFunction = Callable[[SerializerInputType_contra], bytes]
DeserializingFunction = Callable[[bytes], DeserializerOutputType_co]

ChannelArgumentType = Sequence[Tuple[str, Any]]
EOFType: TypeAlias = _EOF
DoneCallbackType = Callable[[Any], None]
RequestIterableType = Union[Iterable[RequestType], AsyncIterable[RequestType]]
ResponseIterableType = AsyncIterable[ResponseType]
