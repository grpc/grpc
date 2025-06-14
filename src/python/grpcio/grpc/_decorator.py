# Copyright 2020 The gRPC authors.
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
"""Defines gRPC-internal decorators."""

import functools
import warnings

_EXPERIMENTAL_APIS_USED = set()


class ExperimentalApiWarning(Warning):
    """A warning that an API is experimental."""


def _warn_experimental(api_name, stack_offset):
    if api_name not in _EXPERIMENTAL_APIS_USED:
        _EXPERIMENTAL_APIS_USED.add(api_name)
        msg = (f"'{api_name}' is an experimental API. It is subject to change or " + "removal between minor releases. Proceed with caution.")
        warnings.warn(msg, ExperimentalApiWarning, stacklevel=2 + stack_offset)


def experimental_api(f):

    @functools.wraps(f)
    def _wrapper(*args, **kwargs):
        _warn_experimental(f.__name__, 1)
        return f(*args, **kwargs)

    return _wrapper
