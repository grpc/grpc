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
"""gRPC's experimental APIs.

These APIs are subject to be removed during any minor version release.
"""

import copy
import functools
import sys
import warnings
from typing import Callable, Optional, Union

import grpc
from grpc._cython import cygrpc as _cygrpc

_EXPERIMENTAL_APIS_USED = set()


class ChannelOptions(object):
    """Indicates a channel option unique to gRPC Python.

    This enumeration is part of an EXPERIMENTAL API.

    Attributes:
      SingleThreadedUnaryStream: Perform unary-stream RPCs on a single thread.
    """

    SingleThreadedUnaryStream = "SingleThreadedUnaryStream"


class UsageError(Exception):
    """Raised by the gRPC library to indicate usage not allowed by the API."""


# It's important that there be a single insecure credentials object so that its
# hash is deterministic and can be used for indexing in the simple stubs cache.
_insecure_channel_credentials = grpc.ChannelCredentials(
    _cygrpc.channel_credentials_insecure()
)


def insecure_channel_credentials():
    """Creates a ChannelCredentials for use with an insecure channel.

    THIS IS AN EXPERIMENTAL API.
    """
    return _insecure_channel_credentials


class ExperimentalApiWarning(Warning):
    """A warning that an API is experimental."""


def _warn_experimental(api_name, stack_offset):
    if api_name not in _EXPERIMENTAL_APIS_USED:
        _EXPERIMENTAL_APIS_USED.add(api_name)
        msg = (
            "'{}' is an experimental API. It is subject to change or ".format(
                api_name
            )
            + "removal between minor releases. Proceed with caution."
        )
        warnings.warn(msg, ExperimentalApiWarning, stacklevel=2 + stack_offset)


def experimental_api(f):
    @functools.wraps(f)
    def _wrapper(*args, **kwargs):
        _warn_experimental(f.__name__, 1)
        return f(*args, **kwargs)

    return _wrapper


def wrap_server_method_handler(wrapper, handler):
    """Wraps the server method handler function.

    The server implementation requires all server handlers being wrapped as
    RpcMethodHandler objects. This helper function ease the pain of writing
    server handler wrappers.

    Args:
        wrapper: A wrapper function that takes in a method handler behavior
          (the actual function) and returns a wrapped function.
        handler: A RpcMethodHandler object to be wrapped.

    Returns:
        A newly created RpcMethodHandler.
    """
    if not handler:
        return None

    if not handler.request_streaming:
        if not handler.response_streaming:
            # NOTE(lidiz) _replace is a public API:
            #   https://docs.python.org/dev/library/collections.html
            return handler._replace(unary_unary=wrapper(handler.unary_unary))
        return handler._replace(unary_stream=wrapper(handler.unary_stream))
    if not handler.response_streaming:
        return handler._replace(stream_unary=wrapper(handler.stream_unary))
    return handler._replace(stream_stream=wrapper(handler.stream_stream))

# A Callable to return in the async case
PrivateKeySignCancel = Callable[[], None]
PrivateKeySignatureAlgorithm = _cygrpc.SignatureAlgorithm
PrivateKeyOnComplete = _cygrpc.OnCompleteWrapper

# Note: SignatureAlgorithm corresponds to C-core's enum class SignatureAlgorithm.
# A function for a user to implement
# Returns signed bytes and accepts bytes to sign and a signature algorithm.
CustomPrivateKeySignWithHandle = Callable[
    [
        bytes,
        PrivateKeySignatureAlgorithm,
        PrivateKeyOnComplete,
    ],
    Union[bytes, PrivateKeySignCancel],
]


def ssl_channel_credentials_with_custom_signer(
    *,
    private_key_sign_fn: CustomPrivateKeySignWithHandle,
    root_certificates: Optional[bytes] = None,
    certificate_chain: bytes,
) -> grpc.ChannelCredentials:
    """Creates a ChannelCredentials for use with an SSL-enabled Channel with a custom signer.

    Args:
      private_key_sign_fn: a function with the signature of
        `CustomPrivateKeySignWithHandle`. This function can return synchronoulsy
        or asynchronously.  To return synchronously, return the signed bytes.
        To return asynchronously, return a PrivateKeySignCancel callable. This
        can be a no-op function if no cancellation is needed. It can also be a
        Python object with __call__(self) implemented for the implementer to
        store async state and cancellation logic on.  synchronously and quickly,
        then call the passed in `on_complete` when the async signing operation
        is complete.
      root_certificates: The PEM-encoded root certificates as a byte string,
        or None to retrieve them from a default location chosen by gRPC
        runtime.
      certificate_chain: The PEM-encoded certificate chain as a byte string
        to use or None if no certificate chain should be used.

    Returns:
      A ChannelCredentials for use with an SSL-enabled Channel.
    """
    return grpc.ChannelCredentials(
        _cygrpc.SSLChannelCredentials(
            root_certificates, None, certificate_chain, private_key_sign_fn
        )
    )


__all__ = (
    "ChannelOptions",
    "ExperimentalApiWarning",
    "UsageError",
    "insecure_channel_credentials",
    "wrap_server_method_handler",
    "ssl_channel_credentials_with_custom_signer",
)

if sys.version_info > (3, 6):
    from grpc._simple_stubs import stream_stream
    from grpc._simple_stubs import stream_unary
    from grpc._simple_stubs import unary_stream
    from grpc._simple_stubs import unary_unary

    __all__ += (
        "stream_stream",
        "stream_unary",
        "unary_stream",
        "unary_unary",
    )
