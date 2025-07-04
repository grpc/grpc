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

from typing import Any

from grpc_testing._channel import _channel
from grpc_testing._channel import _channel_state


# descriptors is reserved for later use.
# pylint: disable=unused-argument
def testing_channel(descriptors: Any, time: Any) -> _channel.TestingChannel:
    return _channel.TestingChannel(time, _channel_state.State())


# pylint: enable=unused-argument
