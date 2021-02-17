# Copyright 2020 gRPC authors.
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
from absl import flags
import googleapiclient.discovery

# GCP
PROJECT = flags.DEFINE_string("project",
                              default=None,
                              help="GCP Project ID. Required")
NAMESPACE = flags.DEFINE_string(
    "namespace",
    default=None,
    help="Isolate GCP resources using given namespace / name prefix. Required")
NETWORK = flags.DEFINE_string("network",
                              default="default",
                              help="GCP Network ID")
# Mirrors --xds-server-uri argument of Traffic Director gRPC Bootstrap
XDS_SERVER_URI = flags.DEFINE_string(
    "xds_server_uri",
    default=None,
    help="Override Traffic Director server uri, for testing")

# Test server
SERVER_NAME = flags.DEFINE_string("server_name",
                                  default="psm-grpc-server",
                                  help="Server deployment and service name")
SERVER_PORT = flags.DEFINE_integer("server_port",
                                   default=8080,
                                   lower_bound=0,
                                   upper_bound=65535,
                                   help="Server test port")
SERVER_MAINTENANCE_PORT = flags.DEFINE_integer(
    "server_maintenance_port",
    lower_bound=0,
    upper_bound=65535,
    default=None,
    help="Server port running maintenance services: health check, channelz, etc"
)
SERVER_XDS_HOST = flags.DEFINE_string("server_xds_host",
                                      default='xds-test-server',
                                      help="Test server xDS hostname")
SERVER_XDS_PORT = flags.DEFINE_integer("server_xds_port",
                                       default=8000,
                                       help="Test server xDS port")

# Test client
CLIENT_NAME = flags.DEFINE_string("client_name",
                                  default="psm-grpc-client",
                                  help="Client deployment and service name")
CLIENT_PORT = flags.DEFINE_integer("client_port",
                                   default=8079,
                                   help="Client test port")

flags.mark_flags_as_required([
    "project",
    "namespace",
])
