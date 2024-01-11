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
"""Constants and functions for data used in interoperability testing."""

import argparse
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


def parse_bool(value):
    if value == "true":
        return True
    if value == "false":
        return False
    raise argparse.ArgumentTypeError("Only true/false allowed")
