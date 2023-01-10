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

from typing import Iterator

from google.protobuf import descriptor  # pytype: disable=pyi-error
from grpc_testing import Channel
from grpc_testing._channel import _channel  # pytype: disable=pyi-error
from grpc_testing._channel import _channel_state  # pytype: disable=pyi-error


# descriptors is reserved for later use.
# pylint: disable=unused-argument
def testing_channel(descriptors: Iterator[descriptor.ServiceDescriptor],
                    time: float) -> Channel:
    return _channel.TestingChannel(time, _channel_state.State())


# pylint: enable=unused-argument
