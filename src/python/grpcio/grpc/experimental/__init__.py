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

import functools
import sys
import warnings

import grpc

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


_insecure_channel_credentials = object()


def insecure_channel_credentials():
    """Creates a ChannelCredentials for use with an insecure channel.

    THIS IS AN EXPERIMENTAL API.

    This is not for use with secure_channel function. Intead, this should be
    used with grpc.unary_unary, grpc.unary_stream, grpc.stream_unary, or
    grpc.stream_stream.
    """
    return grpc.ChannelCredentials(_insecure_channel_credentials)


class ExperimentalApiWarning(Warning):
    """A warning that an API is experimental."""


def _warn_experimental(api_name, stack_offset):
    if api_name not in _EXPERIMENTAL_APIS_USED:
        _EXPERIMENTAL_APIS_USED.add(api_name)
        msg = ("'{}' is an experimental API. It is subject to change or ".
               format(api_name) +
               "removal between minor releases. Proceed with caution.")
        warnings.warn(msg, ExperimentalApiWarning, stacklevel=2 + stack_offset)


def experimental_api(f):

    @functools.wraps(f)
    def _wrapper(*args, **kwargs):
        _warn_experimental(f.__name__, 1)
        return f(*args, **kwargs)

    return _wrapper


__all__ = (
    'ChannelOptions',
    'ExperimentalApiWarning',
    'UsageError',
    'insecure_channel_credentials',
)

if sys.version_info[0] == 3 and sys.version_info[1] >= 6:
    from grpc._simple_stubs import unary_unary, unary_stream, stream_unary, stream_stream
    __all__ = __all__ + (unary_unary, unary_stream, stream_unary, stream_stream)
