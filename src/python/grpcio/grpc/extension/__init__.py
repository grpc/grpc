# Copyright 2018 gRPC authors.
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
"""gRPC Python extension API."""


def secure_channel(target, credentials, options=None):
    """Creates a secure Channel to a server.

    Args:
      target: The server address.
      credentials: A grpc.ChannelCredentials instance.
      options: An optional list of key-value pairs (channel args in gRPC Core
        runtime and/or gRPC extension.) to configure the channel.

    Returns:
      A grpc.Channel object.
    """
    del target, credentials, options
    raise NotImplementedError()


__all__ = ('secure_channel',)
