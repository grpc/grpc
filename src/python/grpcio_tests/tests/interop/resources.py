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
from grpc._cython import cygrpc as _cygrpc

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


def old_client_private_key_signer(data_to_sign, signature_algorithm):
    """
    Of type CustomPrivateKeySign - Callable[[bytes, SignatureAlgorithm], bytes]
    Takes in data_to_sign and signs it using the test private key
    """
    private_key_bytes = client_private_key()
    # Determine the key type and apply appropriate padding and algorithm.
    # This example assumes an RSA key. Different logic is needed for other key types (e.g., EC).
    try:
        success = check_key_cert_match(client_private_key(), client_certificate_chain())
        if not success:
            return ValueError("provided key and certificate do not match.")
        private_key = serialization.load_pem_private_key(
            private_key_bytes,
            password=None,  # Pass password as bytes if the key is encrypted
            backend=default_backend(),
        )
        if not isinstance(private_key, rsa.RSAPrivateKey):
            raise ValueError("The provided key is not an RSA private key.")
    except Exception as e:
        raise

    if isinstance(private_key, rsa.RSAPrivateKey):
        try:
            hasher = hashes.SHA256()
            pss_padding = padding.PSS(
                mgf=padding.MGF1(hasher),
                salt_length=hasher.digest_size,
            )

            signature = private_key.sign(data_to_sign, pss_padding, hasher)
            return signature
        except Exception as e:
            raise
    else:
        return ValueError(
            "Unsupported private key type. This example only supports RSA."
        )


def sync_client_private_key_signer(
    data_to_sign, signature_algorithm, on_complete, completion_data
):
    """
    Of type CustomPrivateKeySign - Callable[[bytes, SignatureAlgorithm], bytes]
    Takes in data_to_sign and signs it using the test private key
    """
    private_key_bytes = client_private_key()
    # Determine the key type and apply appropriate padding and algorithm.
    # This example assumes an RSA key. Different logic is needed for other key types (e.g., EC).
    try:
        success = check_key_cert_match(client_private_key(), client_certificate_chain())
        if not success:
            return ValueError("provided key and certificate do not match.")
        private_key = serialization.load_pem_private_key(
            private_key_bytes,
            password=None,  # Pass password as bytes if the key is encrypted
            backend=default_backend(),
        )
        if not isinstance(private_key, rsa.RSAPrivateKey):
            raise ValueError("The provided key is not an RSA private key.")
    except Exception as e:
        raise

    if isinstance(private_key, rsa.RSAPrivateKey):
        try:
            hasher = hashes.SHA256()
            pss_padding = padding.PSS(
                mgf=padding.MGF1(hasher),
                salt_length=hasher.digest_size,
            )

            signature = private_key.sign(data_to_sign, pss_padding, hasher)
            return signature
        except Exception as e:
            raise
    else:
        return ValueError(
            "Unsupported private key type. This example only supports RSA."
        )


def async_signer_worker(
    data_to_sign, signature_algorithm, on_complete, completion_data
):
    print("GREG: async signer worker", flush=True)
    private_key_bytes = client_private_key()
    # Determine the key type and apply appropriate padding and algorithm.
    # This example assumes an RSA key. Different logic is needed for other key types (e.g., EC).
    try:
        success = check_key_cert_match(client_private_key(), client_certificate_chain())
        if not success:
            on_complete(ValueError("provided key and certificate do not match."))
        private_key = serialization.load_pem_private_key(
            private_key_bytes,
            password=None,  # Pass password as bytes if the key is encrypted
            backend=default_backend(),
        )
        if not isinstance(private_key, rsa.RSAPrivateKey):
            raise ValueError("The provided key is not an RSA private key.")
    except Exception as e:
        raise

    if isinstance(private_key, rsa.RSAPrivateKey):
        try:
            hasher = hashes.SHA256()
            pss_padding = padding.PSS(
                mgf=padding.MGF1(hasher),
                salt_length=hasher.digest_size,
            )

            signature = private_key.sign(data_to_sign, pss_padding, hasher)
            on_complete(signature)
        except Exception as e:
            raise
    else:
        on_complete(
            ValueError("Unsupported private key type. This example only supports RSA.")
        )


def bad_async_signer_worker(
    data_to_sign, signature_algorithm, on_complete, completion_data
):
    print("GREG: async signer worker", flush=True)
    # Use the server private key
    private_key_bytes = server_private_key()
    # Determine the key type and apply appropriate padding and algorithm.
    # This example assumes an RSA key. Different logic is needed for other key types (e.g., EC).
    try:
        private_key = serialization.load_pem_private_key(
            private_key_bytes,
            password=None,  # Pass password as bytes if the key is encrypted
            backend=default_backend(),
        )
        if not isinstance(private_key, rsa.RSAPrivateKey):
            raise ValueError("The provided key is not an RSA private key.")
    except Exception as e:
        raise

    if isinstance(private_key, rsa.RSAPrivateKey):
        try:
            hasher = hashes.SHA256()
            pss_padding = padding.PSS(
                mgf=padding.MGF1(hasher),
                salt_length=hasher.digest_size,
            )

            signature = private_key.sign(data_to_sign, pss_padding, hasher)
            on_complete(signature)
        except Exception as e:
            raise
    else:
        on_complete(
            ValueError("Unsupported private key type. This example only supports RSA.")
        )


def bad_async_client_private_key_signer(
    data_to_sign, signature_algorithm, on_complete, completion_data
):
    """
    Of type CustomPrivateKeySign - Callable[[bytes, SignatureAlgorithm], bytes]
    Takes in data_to_sign and signs it using the test private key
    """
    print("GREG: async signer", flush=True)
    signer_thread = threading.Thread(
        target=bad_async_signer_worker,
        args=(data_to_sign, signature_algorithm, on_complete, completion_data),
    ).start()
    # Add something where we put something cancellable on this handle
    return _cygrpc.create_async_signing_handle()


def async_client_private_key_signer(
    data_to_sign, signature_algorithm, on_complete, completion_data
):
    """
    Of type CustomPrivateKeySign - Callable[[bytes, SignatureAlgorithm], bytes]
    Takes in data_to_sign and signs it using the test private key
    """
    print("GREG: async signer", flush=True)
    signer_thread = threading.Thread(
        target=async_signer_worker,
        args=(data_to_sign, signature_algorithm, on_complete, completion_data),
    ).start()
    # Add something where we put something cancellable on this handle
    return _cygrpc.create_async_signing_handle()


def sync_bad_client_private_key_signer(
    data_to_sign, signature_algorithm, on_complete, completion_data
):
    """
    Of type CustomPrivateKeySign - Callable[[bytes, SignatureAlgorithm], bytes]
    Takes in data_to_sign and signs it using the test private key
    """
    # use the server's private key
    private_key_bytes = server_private_key()
    # Determine the key type and apply appropriate padding and algorithm.
    # This example assumes an RSA key. Different logic is needed for other key types (e.g., EC).
    try:
        private_key = serialization.load_pem_private_key(
            private_key_bytes,
            password=None,  # Pass password as bytes if the key is encrypted
            backend=default_backend(),
        )
        if not isinstance(private_key, rsa.RSAPrivateKey):
            raise ValueError("The provided key is not an RSA private key.")
    except Exception as e:
        raise

    if isinstance(private_key, rsa.RSAPrivateKey):
        try:
            hasher = hashes.SHA256()
            pss_padding = padding.PSS(
                mgf=padding.MGF1(hasher),
                salt_length=hasher.digest_size,
            )

            signature = private_key.sign(data_to_sign, pss_padding, hasher)
            return signature
        except Exception as e:
            raise
    else:
        return ValueError(
            "Unsupported private key type. This example only supports RSA."
        )


def async_signer_worker_until_cancel(
    data_to_sign, signature_algorithm, on_complete, completion_data, cancellation_event
):
    while not cancellation_event.is_set():
        try:
            print("GREG: waiting on cancellation", flush=True)
            print("GREG: cancellation_event: ", cancellation_event, flush=True)
            # Use wait() with a timeout to make the thread responsive to cancellation
            cancellation_event.wait(timeout=1)
        except Exception as e:
            print("GREG: Worker thread: exception occurred", e, flush=True)
            break  # Exit on error
    # on_complete(ValueError("cancelled"))
    print("GREG: thread cancellation done", flush=True)


def async_signer_with_test_handle(
    handle, data_to_sign, signature_algorithm, on_complete, completion_data
):
    print("GREG: async signer", flush=True)
    # cancel_event = threading.Event()
    signer_thread = threading.Thread(
        target=async_signer_worker_until_cancel,
        args=(
            data_to_sign,
            signature_algorithm,
            on_complete,
            completion_data,
            handle.cancel_event,
        ),
    ).start()
    # Add something where we put something cancellable on this handle
    # handle = _cygrpc.create_async_signing_handle()
    # handle.cancel_event = cancel_event
    handle.thread = signer_thread
    return handle


def async_client_private_key_signer_with_cancel(
    data_to_sign, signature_algorithm, on_complete, completion_data
):
    """
    Of type CustomPrivateKeySign - Callable[[bytes, SignatureAlgorithm], bytes]
    Takes in data_to_sign and signs it using the test private key
    """
    print("GREG: async signer", flush=True)
    cancel_event = threading.Event()
    signer_thread = threading.Thread(
        target=async_signer_worker_until_cancel,
        args=(
            data_to_sign,
            signature_algorithm,
            on_complete,
            completion_data,
            cancel_event,
        ),
    ).start()
    # Add something where we put something cancellable on this handle
    handle = _cygrpc.create_async_signing_handle()
    handle.cancel_event = cancel_event
    handle.thread = signer_thread
    return handle


def cancel_async(handle):
    print("GREG IN CANCEL_ASYNC", flush=True)
    print("GREG: handle: ", handle, flush=True)
    print("GREG: cancel event: ", handle.cancel_event, flush=True)
    handle.cancel_event.set()
    print("cancel_event set", flush=True)


def parse_bool(value):
    if value == "true":
        return True
    if value == "false":
        return False
    raise argparse.ArgumentTypeError("Only true/false allowed")
