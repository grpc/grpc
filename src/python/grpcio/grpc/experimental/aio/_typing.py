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

from typing import Any, AnyStr, Callable, Sequence, Text, Tuple, TypeVar
from grpc._cython.cygrpc import EOF

RequestType = TypeVar('RequestType')
ResponseType = TypeVar('ResponseType')
SerializingFunction = Callable[[Any], bytes]
DeserializingFunction = Callable[[bytes], Any]
MetadatumType = Tuple[Text, AnyStr]
MetadataType = Sequence[MetadatumType]
ChannelArgumentType = Sequence[Tuple[Text, Any]]
EOFType = type(EOF)
DoneCallbackType = Callable[[Any], None]
