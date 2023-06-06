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

import socket

from absl import flags

from framework.helpers import highlighter

# GCP
PROJECT = flags.DEFINE_string("project",
                              default=None,
                              help="(required) GCP Project ID.")
RESOURCE_PREFIX = flags.DEFINE_string(
    "resource_prefix",
    default=None,
    help=("(required) The prefix used to name GCP resources.\n"
          "Together with `resource_suffix` used to create unique "
          "resource names."))
RESOURCE_SUFFIX = flags.DEFINE_string(
    "resource_suffix",
    default=None,
    help=("The suffix used to name GCP resources.\n"
          "Together with `resource_prefix` used to create unique "
          "resource names.\n"
          "(default: test suite will generate a random suffix, based on suite "
          "resource management preferences)"))
NETWORK = flags.DEFINE_string("network",
                              default="default",
                              help="GCP Network ID")
COMPUTE_API_VERSION = flags.DEFINE_string(
    "compute_api_version",
    default='v1',
    help="The version of the GCP Compute API, e.g., v1, v1alpha")
# Mirrors --xds-server-uri argument of Traffic Director gRPC Bootstrap
XDS_SERVER_URI = flags.DEFINE_string(
    "xds_server_uri",
    default=None,
    help="Override Traffic Director server URI.")
ENSURE_FIREWALL = flags.DEFINE_bool(
    "ensure_firewall",
    default=False,
    help="Ensure the allow-health-check firewall exists before each test case")
FIREWALL_SOURCE_RANGE = flags.DEFINE_list(
    "firewall_source_range",
    default=['35.191.0.0/16', '130.211.0.0/22'],
    help="Update the source range of the firewall rule.")
FIREWALL_ALLOWED_PORTS = flags.DEFINE_list(
    "firewall_allowed_ports",
    default=['8080-8100'],
    help="Update the allowed ports of the firewall rule.")

# Test server
SERVER_NAME = flags.DEFINE_string(
    "server_name",
    default="psm-grpc-server",
    help="The name to use for test server deployments.")
SERVER_PORT = flags.DEFINE_integer(
    "server_port",
    default=8080,
    lower_bound=1,
    upper_bound=65535,
    help="Server test port.\nMust be within --firewall_allowed_ports.")
SERVER_MAINTENANCE_PORT = flags.DEFINE_integer(
    "server_maintenance_port",
    default=None,
    lower_bound=1,
    upper_bound=65535,
    help=("Server port running maintenance services: Channelz, CSDS, Health, "
          "XdsUpdateHealth, and ProtoReflection (optional).\n"
          "Must be within --firewall_allowed_ports.\n"
          "(default: the port is chosen automatically based on "
          "the security configuration)"))
SERVER_XDS_HOST = flags.DEFINE_string(
    "server_xds_host",
    default="xds-test-server",
    help=("The xDS hostname of the test server.\n"
          "Together with `server_xds_port` makes test server target URI, "
          "xds:///hostname:port"))
# Note: port 0 known to represent a request for dynamically-allocated port
# https://en.wikipedia.org/wiki/List_of_TCP_and_UDP_port_numbers#Well-known_ports
SERVER_XDS_PORT = flags.DEFINE_integer(
    "server_xds_port",
    default=8080,
    lower_bound=0,
    upper_bound=65535,
    help=("The xDS port of the test server.\n"
          "Together with `server_xds_host` makes test server target URI, "
          "xds:///hostname:port\n"
          "Must be unique within a GCP project.\n"
          "Set to 0 to select any unused port."))

# Test client
CLIENT_NAME = flags.DEFINE_string(
    "client_name",
    default="psm-grpc-client",
    help="The name to use for test client deployments")
CLIENT_PORT = flags.DEFINE_integer(
    "client_port",
    default=8079,
    lower_bound=1,
    upper_bound=65535,
    help=(
        "The port test client uses to run gRPC services: Channelz, CSDS, "
        "XdsStats, XdsUpdateClientConfigure, and ProtoReflection (optional).\n"
        "Doesn't have to be within --firewall_allowed_ports."))

# Testing metadata
TESTING_VERSION = flags.DEFINE_string(
    "testing_version",
    default=None,
    help="The testing gRPC version branch name. Like master, dev, v1.55.x")

FORCE_CLEANUP = flags.DEFINE_bool(
    "force_cleanup",
    default=False,
    help="Force resource cleanup, even if not created by this test run")

COLLECT_APP_LOGS = flags.DEFINE_bool(
    'collect_app_logs',
    default=False,
    help=('Collect the logs of the xDS Test Client and Server\n'
          f'into the test_app_logs/ directory under the log directory.\n'
          'See --log_dir description for configuring the log directory.'))

# Needed to configure urllib3 socket timeout, which is infinity by default.
SOCKET_DEFAULT_TIMEOUT = flags.DEFINE_float(
    "socket_default_timeout",
    default=60,
    lower_bound=0,
    help=("Set the default timeout in seconds on blocking socket operations.\n"
          "If zero is given, the new sockets have no timeout. "))


def set_socket_default_timeout_from_flag() -> None:
    """A helper to configure default socket timeout from a flag.

    This is known to affect the following pip packages:
      - google-api-python-client: has the default timeout set to 60:
        https://googleapis.github.io/google-api-python-client/docs/epy/googleapiclient.http-module.html#build_http
      - kubernetes: falls back to urllib3 timeout, which is infinity by default:
        https://urllib3.readthedocs.io/en/stable/reference/urllib3.util.html#urllib3.util.Timeout

    NOTE: Must be called _after_ the flags were parsed by absl, but before
          the before KubernetesApiManager or GcpApiManager initialized.
    """
    timeout: float = SOCKET_DEFAULT_TIMEOUT.value
    # None is inf timeout, which is represented by 0 in the flag.
    socket.setdefaulttimeout(None if timeout == 0 else timeout)


flags.adopt_module_key_flags(highlighter)

flags.mark_flags_as_required([
    "project",
    "resource_prefix",
])
