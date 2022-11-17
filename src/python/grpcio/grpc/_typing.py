# Copyright 2022 gRPC authors.
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
"""Common types for gRPC Sync API"""

from typing import Any, Callable, Iterable, Sequence, Tuple, TypeVar, Union

from grpc._cython.cygrpc import EOF

RequestType = TypeVar('RequestType')
ResponseType = TypeVar('ResponseType')
SerializingFunction = Callable[[Any], bytes]
DeserializingFunction = Callable[[bytes], Any]
MetadataType = Sequence[Tuple[str, Union[str, bytes]]]
ChannelArgumentType = Sequence[Tuple[str, Any]]
EOFType = type(EOF)
DoneCallbackType = Callable[[Any], None]
RequestIterableType = Iterable[Any]
ResponseIterableType = Iterable[Any]
