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
from cryptography import x509
import threading
import grpc

_ROOT_CERTIFICATES_RESOURCE_PATH = "credentials/ca.pem"
_PRIVATE_KEY_RESOURCE_PATH = "credentials/server1.key"
_CERTIFICATE_CHAIN_RESOURCE_PATH = "credentials/server1.pem"
_CLIENT_PRIVATE_KEY_RESOURCE_PATH = "credentials/client.key"
_CLIENT_CERTIFICATE_CHAIN_RESOURCE_PATH = "credentials/client.pem"


def test_root_certificates():
    return pkgutil.get_data(__name__, _ROOT_CERTIFICATES_RESOURCE_PATH)


def server_private_key():
    return pkgutil.get_data(__name__, _PRIVATE_KEY_RESOURCE_PATH)


def certificate_chain():
    return pkgutil.get_data(__name__, _CERTIFICATE_CHAIN_RESOURCE_PATH)


def client_private_key():
    return pkgutil.get_data(__name__, _CLIENT_PRIVATE_KEY_RESOURCE_PATH)


def client_certificate_chain():
    return pkgutil.get_data(__name__, _CLIENT_CERTIFICATE_CHAIN_RESOURCE_PATH)


def check_key_cert_match(key_bytes, cert_bytes):
    """Checks if the private key and certificate public key match."""
    try:
        private_key = serialization.load_pem_private_key(
            key_bytes, password=None, backend=default_backend()
        )
        cert = x509.load_pem_x509_certificate(cert_bytes, default_backend())
        public_key = cert.public_key()

        return private_key.public_key().public_numbers() == public_key.public_numbers()
    except Exception as e:
        return False


def sign_private_key(data_to_sign, private_key_bytes, signature_algorithm):
    # Determine the key type and apply appropriate padding and algorithm.
    # This example assumes an RSA key. Different logic is needed for other key types (e.g., EC).
    try:
        success = check_key_cert_match(client_private_key(), client_certificate_chain())
        if not success:
            return ValueError("provided key and certificate do not match.")
        private_key = serialization.load_pem_private_key(
            private_key_bytes,
            password=None,
            backend=default_backend(),
        )
        if not isinstance(private_key, rsa.RSAPrivateKey):
            return ValueError("The provided key is not an RSA private key.")
    except Exception as e:
        return

    if isinstance(private_key, rsa.RSAPrivateKey):
        try:
            if (
                signature_algorithm
                != grpc.experimental.PrivateKeySignatureAlgorithm.RSA_PSS_RSAE_SHA256
            ):
                return ValueError("Expect the private key to be PSS SHA256")
            hasher = hashes.SHA256()
            pss_padding = padding.PSS(
                mgf=padding.MGF1(hasher),
                salt_length=hasher.digest_size,
            )

            signature = private_key.sign(data_to_sign, pss_padding, hasher)
            return signature
        except Exception as e:
            return e
    else:
        return ValueError(
            "Unsupported private key type. This example only supports RSA."
        )


def sync_client_private_key_signer(
    data_to_sign,
    signature_algorithm,
    on_complete,
):
    """
    Of type CustomPrivateKeySign - Callable[[bytes, SignatureAlgorithm], bytes]
    Takes in data_to_sign and signs it using the test private key with a sync return
    """
    private_key_bytes = client_private_key()
    signature = sign_private_key(data_to_sign, private_key_bytes, signature_algorithm)
    return signature


def bad_async_signer_worker(data_to_sign, signature_algorithm, on_complete):
    """
    A signing function that uses a mismatched private key and certificate.
    Specifically, this is used on the client side and should use the client.key
    file, but rather uses the server.key file to generate an incorrect signature
    """
    # Use the server private key and expect failure
    private_key_bytes = server_private_key()
    signature = sign_private_key(data_to_sign, private_key_bytes, signature_algorithm)
    on_complete(signature)


def bad_async_client_private_key_signer(data_to_sign, signature_algorithm, on_complete):
    """
    Of type CustomPrivateKeySign - Callable[[bytes, SignatureAlgorithm], bytes]
    Takes in data_to_sign and signs it using the wrong private key, resulting in handshake failure
    """
    signer_thread = threading.Thread(
        target=bad_async_signer_worker,
        args=(data_to_sign, signature_algorithm, on_complete),
    ).start()
    return no_op_cancel


def async_signer_worker(data_to_sign, signature_algorithm, on_complete):
    """
    Meant to be used as an async function for a thread, for example
    """
    private_key_bytes = client_private_key()
    signature = sign_private_key(data_to_sign, private_key_bytes, signature_algorithm)
    on_complete(signature)

def no_op_cancel():
    pass

def async_client_private_key_signer(data_to_sign, signature_algorithm, on_complete):
    """
    Of type CustomPrivateKeySign - Callable[[bytes, SignatureAlgorithm], bytes]
    Takes in data_to_sign and signs it using the test private key
    """
    signer_thread = threading.Thread(
        target=async_signer_worker,
        args=(data_to_sign, signature_algorithm, on_complete),
    ).start()
    # Add something where we put something cancellable on this handle
    return no_op_cancel


def sync_bad_client_private_key_signer(data_to_sign, signature_algorithm, on_complete):
    """
    Of type CustomPrivateKeySign - Callable[[bytes, SignatureAlgorithm], bytes]
    Takes in data_to_sign and signs it using the wrong private key and returns synchronously
    """
    # use the server's private key
    private_key_bytes = server_private_key()
    signature = sign_private_key(data_to_sign, private_key_bytes, signature_algorithm)
    return signature

class CancelCallable:
    def __init__(self):
        self.cancel_event = threading.Event()
        self.handshake_started_event = threading.Event()

    def __call__(self):
        self.cancel_event.set()


def async_signer_worker_until_cancel(
    data_to_sign,
    signature_algorithm,
    on_complete,
    cancellation_event,
    handshake_started_event,
):
    """
    Infinitely loops until cancelled for testing cancellation
    """
    handshake_started_event.set()
    while not cancellation_event.is_set():
        try:
            # Use wait() with a timeout to make the thread responsive to cancellation
            cancellation_event.wait(timeout=1)
        except Exception as e:
            raise


def async_signer_with_cancel_injection(
    cancel_callable, data_to_sign, signature_algorithm, on_complete
):
    """
    A helper for an async signer that uses a handle provided by the test.
    This makes the values of the handle available to the test.
    Runs infinitely until cancelled, and the passed handle should then have the
    handle.cancel_event set
    """
    signer_thread = threading.Thread(
        target=async_signer_worker_until_cancel,
        args=(
            data_to_sign,
            signature_algorithm,
            on_complete,
            cancel_callable.cancel_event,
            cancel_callable.handshake_started_event,
        ),
    ).start()
    return cancel_callable


def async_client_private_key_signer_with_cancel(
    data_to_sign, signature_algorithm, on_complete
):
    """
    Of type CustomPrivateKeySign - Callable[[bytes, SignatureAlgorithm], bytes]
    Takes in data_to_sign and signs it using the test private key
    Runs infinitely until cancelled
    """
    cancel = CancelCallable()
    signer_thread = threading.Thread(
        target=async_signer_worker_until_cancel,
        args=(
            data_to_sign,
            signature_algorithm,
            on_complete,
            cancel.cancel_event,
            cancel.handshake_started_event,
        ),
    ).start()
    # Add something where we put something cancellable on this handle
    return cancel


def parse_bool(value):
    if value == "true":
        return True
    if value == "false":
        return False
    raise argparse.ArgumentTypeError("Only true/false allowed")
