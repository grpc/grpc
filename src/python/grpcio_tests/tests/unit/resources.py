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
"""Constants and functions for data used in testing."""

import os
import pkgutil

_ROOT_CERTIFICATES_RESOURCE_PATH = "credentials/ca.pem"
_PRIVATE_KEY_RESOURCE_PATH = "credentials/server1.key"
_CERTIFICATE_CHAIN_RESOURCE_PATH = "credentials/server1.pem"


def test_root_certificates():
    return pkgutil.get_data(__name__, _ROOT_CERTIFICATES_RESOURCE_PATH)


def private_key():
    return pkgutil.get_data(__name__, _PRIVATE_KEY_RESOURCE_PATH)


def certificate_chain():
    return pkgutil.get_data(__name__, _CERTIFICATE_CHAIN_RESOURCE_PATH)


def cert_hier_1_root_ca_cert():
    return pkgutil.get_data(
        __name__, "credentials/certificate_hierarchy_1/certs/ca.cert.pem"
    )


def cert_hier_1_intermediate_ca_cert():
    return pkgutil.get_data(
        __name__,
        "credentials/certificate_hierarchy_1/intermediate/certs/intermediate.cert.pem",
    )


def cert_hier_1_client_1_key():
    return pkgutil.get_data(
        __name__,
        "credentials/certificate_hierarchy_1/intermediate/private/client.key.pem",
    )


def cert_hier_1_client_1_cert():
    return pkgutil.get_data(
        __name__,
        "credentials/certificate_hierarchy_1/intermediate/certs/client.cert.pem",
    )


def cert_hier_1_server_1_key():
    return pkgutil.get_data(
        __name__,
        "credentials/certificate_hierarchy_1/intermediate/private/localhost-1.key.pem",
    )


def cert_hier_1_server_1_cert():
    return pkgutil.get_data(
        __name__,
        "credentials/certificate_hierarchy_1/intermediate/certs/localhost-1.cert.pem",
    )


def cert_hier_2_root_ca_cert():
    return pkgutil.get_data(
        __name__, "credentials/certificate_hierarchy_2/certs/ca.cert.pem"
    )


def cert_hier_2_intermediate_ca_cert():
    return pkgutil.get_data(
        __name__,
        "credentials/certificate_hierarchy_2/intermediate/certs/intermediate.cert.pem",
    )


def cert_hier_2_client_1_key():
    return pkgutil.get_data(
        __name__,
        "credentials/certificate_hierarchy_2/intermediate/private/client.key.pem",
    )


def cert_hier_2_client_1_cert():
    return pkgutil.get_data(
        __name__,
        "credentials/certificate_hierarchy_2/intermediate/certs/client.cert.pem",
    )


def cert_hier_2_server_1_key():
    return pkgutil.get_data(
        __name__,
        "credentials/certificate_hierarchy_2/intermediate/private/localhost-1.key.pem",
    )


def cert_hier_2_server_1_cert():
    return pkgutil.get_data(
        __name__,
        "credentials/certificate_hierarchy_2/intermediate/certs/localhost-1.cert.pem",
    )
