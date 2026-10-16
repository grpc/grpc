# Copyright 2021 The gRPC Authors
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
"""gRPC Python's Admin interface."""

from grpc_channelz.v1 import channelz
import grpc_csds


def add_admin_servicers(server):
    """Register admin servicers to a server.

    gRPC provides some predefined admin services to make debugging easier by
    exposing gRPC's internal states. Each existing admin service is packaged as
    a separate library, and the documentation of the predefined admin services
    is usually scattered. It can be time consuming to get the dependency
    management, module initialization, and library import right for each one of
    them.

    This API provides a convenient way to create a gRPC server to expose admin
    services. With this, any new admin services that you may add in the future
    are automatically available via the admin interface just by upgrading your
    gRPC version.

    Args:
        server: A gRPC server to which all admin services will be added.
    """
    channelz.add_channelz_servicer(server)
    grpc_csds.add_csds_servicer(server)


__all__ = ["add_admin_servicers"]
