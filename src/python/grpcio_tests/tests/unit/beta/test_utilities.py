# Copyright 2015 gRPC authors.
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
"""Test-appropriate entry points into the gRPC Python Beta API."""

import grpc
from grpc.beta import implementations


def not_really_secure_channel(
    host, port, channel_credentials, server_host_override
):
    """Creates an insecure Channel to a remote host.

    Args:
      host: The name of the remote host to which to connect.
      port: The port of the remote host to which to connect.
      channel_credentials: The implementations.ChannelCredentials with which to
        connect.
      server_host_override: The target name used for SSL host name checking.

    Returns:
      An implementations.Channel to the remote host through which RPCs may be
        conducted.
    """
    target = "%s:%d" % (host, port)
    channel = grpc.secure_channel(
        target,
        channel_credentials,
        (
            (
                "grpc.ssl_target_name_override",
                server_host_override,
            ),
        ),
    )
    return implementations.Channel(channel)
