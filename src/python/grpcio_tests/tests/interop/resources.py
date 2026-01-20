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
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import padding
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.backends import default_backend
from cryptography.hazmat.primitives.asymmetric import rsa

_ROOT_CERTIFICATES_RESOURCE_PATH = "credentials/ca.pem"
_PRIVATE_KEY_RESOURCE_PATH = "credentials/server1.key"
_CERTIFICATE_CHAIN_RESOURCE_PATH = "credentials/server1.pem"
_CLIENT_PRIVATE_KEY_RESOURCE_PATH = "credentials/client.key"
_CLIENT_CERTIFICATE_CHAIN_RESOURCE_PATH = "credentials/client.pem"


def test_root_certificates():
    return pkgutil.get_data(__name__, _ROOT_CERTIFICATES_RESOURCE_PATH)


def private_key():
    return pkgutil.get_data(__name__, _PRIVATE_KEY_RESOURCE_PATH)


def certificate_chain():
    return pkgutil.get_data(__name__, _CERTIFICATE_CHAIN_RESOURCE_PATH)


def client_private_key():
    return pkgutil.get_data(__name__, _CLIENT_PRIVATE_KEY_RESOURCE_PATH)


def client_certificate_chain():
    return pkgutil.get_data(__name__, _CLIENT_CERTIFICATE_CHAIN_RESOURCE_PATH)


# Of type CustomPrivateKeySign - Callable[[bytes, SignatureAlgorithm], bytes]
def client_private_key_signer(data_to_sign, signature_algorithm):
    private_key = client_private_key()
    # Determine the key type and apply appropriate padding and algorithm.
    # This example assumes an RSA key. Different logic is needed for other key types (e.g., EC).
    if isinstance(private_key, rsa.RSAPrivateKey):
        try:
            signature = private_key.sign(
                data_to_sign,
                padding.PSS(
                    mgf=padding.MGF1(hashes.SHA256()),
                    salt_length=padding.PSS.MAX_LENGTH,
                ),
                hashes.SHA256(),
            )
            return signature
        except Exception as e:
            print(f"Error signing data: {e}")
            return None
    else:
        print("Unsupported private key type. This example only supports RSA.")
        return None


def parse_bool(value):
    if value == "true":
        return True
    if value == "false":
        return False
    raise argparse.ArgumentTypeError("Only true/false allowed")
